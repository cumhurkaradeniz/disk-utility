// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "images/blankimage.h"
#include "images/imagefile.h"

#include "dialogs/volume_format.h"

#include "udisks/model.h"
#include "udisks/operations.h"

#include <algorithm>

#include <unistd.h>

namespace images {

static constexpr uint64_t kFirstPartitionOffset = 1ull << 20;

BlankImageMaker::BlankImageMaker(udisks::Model& model, udisks::Operations& operations)
    : m_model(model)
    , m_operations(operations)
{
}

BlankImageMaker::~BlankImageMaker() = default;

template<typename F>
auto BlankImageMaker::guard(F f)
{
    return [this, alive = std::weak_ptr<bool>(m_alive), f = std::move(f)](const udisks::Result& result) {
        if (!alive.expired())
            f(result);
    };
}

void BlankImageMaker::step(Step step)
{
    if (onStep)
        onStep(step);
}

void BlankImageMaker::start(BlankImage image)
{
    if (m_running)
        return;
    m_running = true;
    m_image = std::move(image);
    m_outcome = {};
    step(Step::Creating);
    const std::string error = createBlankFile(m_image.path, m_image.size);
    if (!error.empty()) {
        Outcome outcome;
        outcome.error = error;
        // The file is someone else's or was never made: nothing to undo.
        m_running = false;
        if (onFinished)
            onFinished(outcome);
        return;
    }
    attach();
}

void BlankImageMaker::attach()
{
    step(Step::Attaching);
    m_operations.loopSetup(m_image.path, false, guard([this](const udisks::Result& result) {
        if (!result.ok() || result.objectPath().empty()) {
            fail(result, result.errorMessage);
            return;
        }
        m_outcome.loop = result.objectPath();
        if (m_image.scheme == Scheme::None)
            format();
        else
            partition();
    }));
}

static udisks::FormatOptions formatOptions(const BlankImage& image)
{
    udisks::FormatOptions options;
    options.label = image.name;
    options.passphrase = image.passphrase;
    return options;
}

void BlankImageMaker::partition()
{
    step(Step::Partitioning);
    m_operations.format(m_outcome.loop, tableType(m_image.scheme), {}, guard([this](const udisks::Result& result) {
        if (!result.ok()) {
            fail(result, result.errorMessage);
            return;
        }
        step(Step::Formatting);
        const bool encrypted = !m_image.passphrase.empty();
        // MBR partitions have no names.
        const std::string name = m_image.scheme == Scheme::Gpt ? m_image.name : std::string();
        // A size of 0 takes all the space after the offset.
        const std::string type = dialogs::partitionTypeFor(tableType(m_image.scheme), m_image.filesystem, encrypted);
        m_operations.createPartitionAndFormat(
            m_outcome.loop, kFirstPartitionOffset, 0, type, name, m_image.filesystem, formatOptions(m_image),
            guard([this](const udisks::Result& created) {
                if (!created.ok() || created.objectPath().empty()) {
                    fail(created, created.errorMessage);
                    return;
                }
                formatted(created.objectPath());
            }));
    }));
}

void BlankImageMaker::format()
{
    step(Step::Formatting);
    m_operations.format(m_outcome.loop, m_image.filesystem, formatOptions(m_image),
                        guard([this](const udisks::Result& result) {
                            if (!result.ok()) {
                                fail(result, result.errorMessage);
                                return;
                            }
                            formatted(m_outcome.loop);
                        }));
}

void BlankImageMaker::formatted(const std::string& block)
{
    m_outcome.ok = true;
    m_outcome.volume = block;
    if (m_image.passphrase.empty()) {
        mount();
        return;
    }
    // The format leaves an encrypted volume unlocked, its cleartext block
    // known before the call returns; unlock it should that have changed.
    if (const udisks::Object* cleartext = m_model.cleartextOf(block)) {
        forgetPassphrase();
        m_outcome.volume = cleartext->path;
        mount();
        return;
    }
    const std::string passphrase = m_image.passphrase;
    forgetPassphrase();
    m_operations.unlock(block, passphrase, guard([this](const udisks::Result& result) {
        if (!result.ok() || result.objectPath().empty()) {
            m_outcome.volume.clear();
            if (!result.cancelled())
                m_outcome.mountError = result.errorMessage;
            finish(std::move(m_outcome));
            return;
        }
        m_outcome.volume = result.objectPath();
        mount();
    }));
}

void BlankImageMaker::mount()
{
    step(Step::Mounting);
    m_operations.mount(m_outcome.volume, guard([this](const udisks::Result& result) {
        if (result.ok())
            m_outcome.mountPoint = result.string();
        else if (!result.cancelled())
            m_outcome.mountError = result.errorMessage;
        finish(std::move(m_outcome));
    }));
}

void BlankImageMaker::fail(const udisks::Result& result, const std::string& error)
{
    Outcome outcome;
    outcome.cancelled = result.cancelled();
    outcome.error = error;
    const std::string loop = m_outcome.loop;
    const std::string path = m_image.path;
    if (loop.empty()) {
        ::unlink(path.c_str());
        finish(std::move(outcome));
        return;
    }
    // The file goes once nothing has it open.
    m_operations.loopDelete(loop, guard([this, path, outcome](const udisks::Result&) {
        ::unlink(path.c_str());
        finish(outcome);
    }));
}

void BlankImageMaker::forgetPassphrase()
{
    std::fill(m_image.passphrase.begin(), m_image.passphrase.end(), '\0');
    m_image.passphrase.clear();
}

void BlankImageMaker::finish(Outcome outcome)
{
    forgetPassphrase();
    m_running = false;
    if (onFinished)
        onFinished(outcome);
}

} // namespace images
