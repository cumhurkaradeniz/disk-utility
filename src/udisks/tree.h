// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>
#include <vector>

namespace udisks {

class Model;

enum class Section {
    Internal,
    External,
    Images,
};

enum class ViewMode {
    // Each disk's volumes, and a disk that has none as itself. Partitions
    // UDisks2 hints to ignore, such as an EFI system partition, stay out.
    Volumes,
    // Disk, then its partitions, extended partitions holding their
    // logical ones, and volumes.
    AllDevices,
};

enum class NodeKind {
    // A whole disk: a drive's medium or an attached disk image.
    Disk,
    // An extended partition holding logical ones.
    Container,
    // A partition with no filesystem or encryption UDisks2 can use: free
    // for erasing, swap, a boot partition.
    Partition,
    // A filesystem, or an encrypted block that unlocks to one. For a disk
    // with a filesystem and no partition table, the disk's own block.
    Volume,
};

// One row of the sidebar, and what a selection holds.
struct Node
{
    NodeKind kind = NodeKind::Disk;
    // Unique in the tree and stable across updates: the kind's letter and
    // the object path, "d:", "c:", "p:" or "v:" before it.
    std::string id;
    // The drive object; empty for a disk image.
    std::string drive;
    // The block device object: the whole disk for a disk, otherwise the
    // partition's (or the disk's, for a volume without a table). For an
    // encrypted volume, the encrypted block; Model::contentOf() gives its
    // cleartext block.
    std::string block;
    // The whole disk's block, the same as block for a disk.
    std::string disk;
    std::vector<Node> children;

    bool operator==(const Node&) const = default;
};

struct SectionNodes
{
    Section section = Section::Internal;
    std::vector<Node> nodes;

    bool operator==(const SectionNodes&) const = default;
};

// Internal, External and Disk Images, in that order, each left out while it
// is empty. Disks sort by UDisks2's sort key, disk images by device.
std::vector<SectionNodes> buildTree(const Model& model, ViewMode mode);
// Depth first; null when it is not in the tree.
const Node* findNode(const std::vector<SectionNodes>& tree, const std::string& id);
// External: removable media, or a drive on USB, an SD card or FireWire.
Section sectionOfDrive(const Model& model, const std::string& drive);

} // namespace udisks
