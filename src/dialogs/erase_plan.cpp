// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "dialogs/erase_plan.h"
#include "dialogs/partition_plan.h"
#include "dialogs/volume_format.h"

#include "udisks/describe.h"
#include "udisks/model.h"

namespace dialogs {

bool erasesWholeDisk(const udisks::Node& node)
{
    return node.kind == udisks::NodeKind::Disk;
}

static udisks::FormatOptions optionsFor(const EraseRequest& request)
{
    udisks::FormatOptions options;
    options.label = normalizedLabel(request.filesystem, request.name);
    options.erase = request.zeroFill ? "zero" : "";
    options.passphrase = request.passphrase;
    options.encryptionType = request.passphrase.empty() ? std::string() : request.encryptionType;
    options.takeOwnership = hasOwners(request.filesystem);
    return options;
}

std::vector<PlanStep> eraseSteps(const udisks::Model& model, const udisks::Node& node, const EraseRequest& request)
{
    std::vector<PlanStep> steps;
    const udisks::Object* object = model.object(node.block);
    if (!object || !object->block)
        return steps;
    const std::string subject = udisks::displayName(model, node);
    const std::string label = normalizedLabel(request.filesystem, request.name);

    if (!erasesWholeDisk(node)) {
        steps = teardownSteps(model, *object, subject);
        steps.push_back({.kind = PlanStep::Kind::Format, .path = node.block, .size = object->block->size,
                         .type = request.filesystem, .options = optionsFor(request), .subject = subject,
                         .expected = udisks::identityOf(model, node.block)});
        return steps;
    }

    // Cleartext blocks are unmounted and locked by their encrypted block's
    // teardown; logical partitions come with the rest.
    std::vector<const udisks::Object*> blocks = {object};
    for (const udisks::Object* partition : model.partitionsOf(node.block))
        blocks.push_back(partition);
    for (const udisks::Object* block : blocks) {
        udisks::Node part;
        part.kind = udisks::NodeKind::Volume;
        part.block = block->path;
        part.disk = node.block;
        const std::vector<PlanStep> teardown = teardownSteps(model, *block, udisks::displayName(model, part));
        steps.insert(steps.end(), teardown.begin(), teardown.end());
    }

    udisks::FormatOptions table;
    table.erase = request.zeroFill ? "zero" : "";
    table.takeOwnership = false;
    steps.push_back({.kind = PlanStep::Kind::CreateTable, .path = node.block, .size = object->block->size,
                     .type = request.scheme, .options = table, .subject = subject,
                     .expected = udisks::identityOf(model, node.block)});

    const uint64_t start = usableStart();
    const uint64_t end = usableEnd(object->block->size, request.scheme);
    udisks::FormatOptions options = optionsFor(request);
    // The partition map's erase covered the disk already.
    options.erase.clear();
    std::string partitionName;
    if (request.scheme == "gpt")
        partitionName = partitionNameFor(label);
    steps.push_back({.kind = PlanStep::Kind::Create, .path = node.block, .offset = start,
                     .size = end > start ? end - start : 0, .type = request.filesystem,
                     .partitionName = partitionName, .options = options,
                     .subject = label.empty() ? formatName(request.filesystem) : label,
                     .expected = udisks::identityOf(model, node.block)});
    return steps;
}

} // namespace dialogs
