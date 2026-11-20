// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "udisks/tree.h"
#include "udisks/model.h"

#include <algorithm>
#include <optional>

namespace udisks {

Section sectionOfDrive(const Model& model, const std::string& drive)
{
    const Object* object = model.object(drive);
    if (!object || !object->drive)
        return Section::Internal;
    const Drive& info = *object->drive;
    if (info.removable || info.mediaRemovable)
        return Section::External;
    for (const char* bus : {"usb", "sdio", "ieee1394"}) {
        if (info.connectionBus == bus)
            return Section::External;
    }
    return Section::Internal;
}

static bool isVolume(const Object& block)
{
    return block.filesystem || block.encrypted;
}

static Node makeNode(NodeKind kind, const std::string& block, const std::string& drive, const std::string& disk)
{
    static constexpr const char* prefixes[] = {"d:", "c:", "p:", "v:"};
    Node node;
    node.kind = kind;
    node.id = prefixes[int(kind)] + block;
    node.drive = drive;
    node.block = block;
    node.disk = disk;
    return node;
}

static Node partitionNode(const Object& partition, const std::string& drive, const std::string& disk)
{
    if (partition.partition->isContainer)
        return makeNode(NodeKind::Container, partition.path, drive, disk);
    return makeNode(isVolume(partition) ? NodeKind::Volume : NodeKind::Partition, partition.path, drive, disk);
}

// A disk with its partitions, logical ones inside their extended partition.
static Node diskNode(const Model& model, const Object& whole, const std::string& drive)
{
    Node disk = makeNode(NodeKind::Disk, whole.path, drive, whole.path);
    if (whole.partitionTable) {
        // Children are added after it, so the extended partition is kept by index.
        std::optional<size_t> container;
        uint64_t containerEnd = 0;
        for (const Object* partition : model.partitionsOf(whole.path)) {
            const Partition& info = *partition->partition;
            Node node = partitionNode(*partition, drive, whole.path);
            if (info.isContained && container && info.offset < containerEnd) {
                disk.children[*container].children.push_back(std::move(node));
                continue;
            }
            disk.children.push_back(std::move(node));
            if (info.isContainer) {
                container = disk.children.size() - 1;
                containerEnd = info.offset + info.size;
            }
        }
    } else if (isVolume(whole)) {
        disk.children.push_back(makeNode(NodeKind::Volume, whole.path, drive, whole.path));
    }
    return disk;
}

static void collectVolumes(const Model& model, const Node& node, std::vector<Node>& volumes)
{
    for (const Node& child : node.children) {
        if (child.kind == NodeKind::Volume) {
            const Object* block = model.object(child.block);
            if (block && block->block && block->block->hintIgnore)
                continue;
            volumes.push_back(child);
        } else {
            collectVolumes(model, child, volumes);
        }
    }
}

std::vector<SectionNodes> buildTree(const Model& model, ViewMode mode)
{
    struct Entry
    {
        Section section;
        std::string sortKey;
        uint64_t deviceNumber;
        Node disk;
    };
    std::vector<Entry> entries;

    for (const auto& [path, object] : model.objects()) {
        if (object.drive) {
            const Object* whole = model.wholeBlock(path);
            if (!whole || whole->block->size == 0)
                continue;
            entries.push_back({sectionOfDrive(model, path), object.drive->sortKey, whole->block->deviceNumber,
                               diskNode(model, *whole, path)});
        } else if (object.loop && object.block && object.block->drive.empty() && !object.partition
                   && object.block->cryptoBackingDevice.empty()) {
            // Unused loop devices are listed too, empty.
            if (object.block->size == 0 || object.loop->backingFile.empty() || object.block->hintIgnore)
                continue;
            entries.push_back({Section::Images, {}, object.block->deviceNumber, diskNode(model, object, {})});
        }
    }

    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.section != b.section)
            return a.section < b.section;
        if (a.sortKey != b.sortKey)
            return a.sortKey < b.sortKey;
        return a.deviceNumber < b.deviceNumber;
    });

    std::vector<SectionNodes> tree;
    for (Entry& entry : entries) {
        if (tree.empty() || tree.back().section != entry.section)
            tree.push_back({entry.section, {}});
        std::vector<Node>& nodes = tree.back().nodes;
        if (mode == ViewMode::AllDevices) {
            nodes.push_back(std::move(entry.disk));
            continue;
        }
        std::vector<Node> volumes;
        collectVolumes(model, entry.disk, volumes);
        if (volumes.empty()) {
            entry.disk.children.clear();
            nodes.push_back(std::move(entry.disk));
        } else {
            for (Node& volume : volumes)
                nodes.push_back(std::move(volume));
        }
    }
    return tree;
}

static const Node* findIn(const std::vector<Node>& nodes, const std::string& id)
{
    for (const Node& node : nodes) {
        if (node.id == id)
            return &node;
        if (const Node* found = findIn(node.children, id))
            return found;
    }
    return nullptr;
}

const Node* findNode(const std::vector<SectionNodes>& tree, const std::string& id)
{
    for (const SectionNodes& section : tree) {
        if (const Node* found = findIn(section.nodes, id))
            return found;
    }
    return nullptr;
}

} // namespace udisks
