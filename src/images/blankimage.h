// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "images/formats.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace udisks {
class Model;
class Operations;
struct Result;
}

namespace images {

struct BlankImage
{
    std::string path;
    uint64_t size = 0;
    // UDisks2's filesystem type: "ext4", "exfat", "vfat"...
    std::string filesystem;
    Scheme scheme = Scheme::Gpt;
    // The volume's name: its label, as the filesystem keeps it, and the
    // partition's name on GPT.
    std::string name;
    // Encrypts the volume with LUKS when set.
    std::string passphrase;
};

// Makes a blank disk image: the file, attached as a loop device, its
// partition map and formatted volume, which is then mounted. A failure
// before the volume is made detaches the image and removes the file again.
class BlankImageMaker
{
public:
    enum class Step {
        Creating,
        Attaching,
        Partitioning,
        Formatting,
        Mounting,
    };

    struct Outcome
    {
        bool ok = false;
        // The password dialog was dismissed.
        bool cancelled = false;
        std::string error;
        // The attached image's loop device and its volume (the cleartext
        // block when encrypted).
        std::string loop;
        std::string volume;
        // Empty when mounting failed; the image is made all the same.
        std::string mountPoint;
        std::string mountError;
    };

    BlankImageMaker(udisks::Model& model, udisks::Operations& operations);
    ~BlankImageMaker();

    BlankImageMaker(const BlankImageMaker&) = delete;
    BlankImageMaker& operator=(const BlankImageMaker&) = delete;

    // One image per maker.
    void start(BlankImage image);
    bool isRunning() const { return m_running; }
    // The image being made; its passphrase is forgotten once the volume is
    // unlocked or the maker is done.
    const BlankImage& image() const { return m_image; }

    std::function<void(Step step)> onStep;
    std::function<void(const Outcome& outcome)> onFinished;

private:
    void attach();
    void partition();
    void format();
    void formatted(const std::string& block);
    void mount();
    void fail(const udisks::Result& result, const std::string& error);
    void finish(Outcome outcome);
    void forgetPassphrase();
    void step(Step step);
    // Done callbacks that do nothing once the maker is gone.
    template<typename F>
    auto guard(F f);

    udisks::Model& m_model;
    udisks::Operations& m_operations;
    BlankImage m_image;
    Outcome m_outcome;
    bool m_running = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace images
