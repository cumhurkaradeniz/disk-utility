// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "actions.h"
#include "dialogs/plan_runner.h"
#include "udisks/describe.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace udisks {
class Model;
struct Object;
}

namespace dialogs {

// A stretch of unpartitioned space on a disk with a partition table, from a
// MiB boundary in whole MiB, a MiB kept free at each end for the table, as
// largestFreeSpace() counts it, below 2 TiB on MBR. Logical partitions'
// space inside an extended partition is not free.
struct FreeRegion
{
    uint64_t offset = 0;
    uint64_t size = 0;
};
std::vector<FreeRegion> freeRegions(const udisks::Model& model, const std::string& disk);

// The space a partition map can use on a disk of the size: from 1 MiB to a
// MiB before the end, and below 2 TiB for a Master Boot Record.
uint64_t usableStart();
uint64_t usableEnd(uint64_t diskSize, const std::string& tableType);
// How many partitions the map holds: 4 primary ones on MBR (an extended
// one counting as one), 128 on GPT.
size_t maximumPartitions(const std::string& tableType);

// One slice of the partition editor's disk.
struct Slice
{
    enum class Kind {
        // On the disk now.
        Partition,
        // An MBR extended partition, kept as it is with its logical ones.
        Container,
        // To be created.
        New,
        Free,
    };

    // Stays with the slice through every edit.
    uint32_t id = 0;
    Kind kind = Kind::Free;
    // A partition's block.
    std::string block;
    uint64_t offset = 0;
    uint64_t size = 0;
    // The volume's name to give it, and the filesystem, UDisks2's IdType.
    std::string name;
    std::string filesystem;

    // A partition's state as found.
    uint64_t originalSize = 0;
    std::string originalName;
    std::string originalFilesystem;
    // Its filesystem's block: the cleartext block of an unlocked encrypted
    // partition.
    std::string contentBlock;
    bool mounted = false;
    bool encrypted = false;
    // Its content has UDisks2's Filesystem interface, so it can be renamed.
    bool hasFilesystem = false;
    // Used space, when the filesystem is mounted; 0 unknown: a shrink is
    // then offered down to the format's minimum and the tool decides.
    uint64_t used = 0;
    // What the summary calls it.
    std::string displayName;
    // The partition and its content as read, which each step on them checks
    // again before it runs.
    udisks::Identity identity;
    udisks::Identity contentIdentity;

    uint64_t end() const { return offset + size; }
    bool isEditable() const { return kind == Kind::New || kind == Kind::Partition; }
    // A partition whose filesystem is replaced.
    bool reformats() const { return kind == Kind::Partition && filesystem != originalFilesystem; }
};

// The partition editor's model of a disk: its slices in disk order, edited
// in place, and the steps that make the disk match them. Pure: it reads the
// model once, and the resize abilities as UDisks2 answers them.
class PartitionLayout
{
public:
    // UDisks2's answer to CanResize for a filesystem.
    struct ResizeSupport
    {
        bool available = false;
        uint64_t modes = 0;
        std::string tool;
    };

    // The disk's partitions and free space; a disk without a partition map
    // starts as free space under a new GUID partition map.
    static PartitionLayout fromDisk(const udisks::Model& model, const std::string& disk,
                                    const udisks::StatFunction& stat = udisks::statFilesystem);

    const std::string& disk() const { return m_disk; }
    const std::string& tableType() const { return m_tableType; }
    // The disk has no partition map; the plan starts by creating one.
    bool createsTable() const { return m_createsTable; }
    uint64_t start() const { return m_start; }
    uint64_t end() const { return m_end; }

    const std::vector<Slice>& slices() const { return m_slices; }
    const Slice* slice(uint32_t id) const;
    std::optional<size_t> indexOf(uint32_t id) const;

    // UDisks2's CanResize for a filesystem: the modes (2 offline shrink,
    // 4 offline grow, 8 online shrink, 16 online grow) or the missing tool.
    void setResizeSupport(const std::string& filesystem, bool available, uint64_t modes, const std::string& tool);
    bool hasResizeSupport(const std::string& filesystem) const { return m_resize.count(filesystem) != 0; }
    // The filesystems of the partitions whose resize support is asked for.
    std::vector<std::string> resizableFilesystems() const;

    // Whether the slice can shrink or grow, and why not.
    ActionState shrinkState(uint32_t id) const;
    ActionState growState(uint32_t id) const;

    // A new partition: in the selected free space, or the largest free
    // space, or the second half of the selected slice when there is none.
    ActionState addState(std::optional<uint32_t> selected) const;
    std::optional<uint32_t> add(std::optional<uint32_t> selected, const std::string& filesystem,
                                const std::string& name);
    // Deletes a partition or drops a new one; the space becomes free.
    ActionState removeState(uint32_t id) const;
    bool remove(uint32_t id);

    // The boundary after slice index: between it and the next slice, or the
    // end of the disk's usable space. Moving it resizes the slice, and the
    // next one when that is free or new; shrinking before a partition that
    // stays leaves free space between them.
    bool canMoveBoundary(size_t index) const;
    std::pair<uint64_t, uint64_t> boundaryRange(size_t index) const;
    void moveBoundary(size_t index, uint64_t position);

    // The slice's size, as moving its end boundary allows.
    std::pair<uint64_t, uint64_t> sizeRange(uint32_t id) const;
    void setSize(uint32_t id, uint64_t size);
    void setName(uint32_t id, const std::string& name);
    void setFilesystem(uint32_t id, const std::string& filesystem);

    // Merges neighbouring free space and drops free slices under a MiB,
    // after a drag.
    void normalize();

    bool isChanged() const;
    // Why the plan cannot run, such as a name its filesystem cannot take,
    // a partition too small for its filesystem, or a size its filesystem
    // cannot be resized to; empty when it can.
    std::string problem() const;
    // Shrinks first (the steps a tool is likeliest to refuse, before
    // anything is deleted), then deletes, grows, erases, creates and
    // renames, each in disk order: no step needs space a later one frees.
    // Nothing while problem() has something to say.
    std::vector<PlanStep> plan() const;
    // What is mounted and unlocked now, for the unmount and lock steps of
    // the plan; reading the disk took it once.
    void updateTeardown(const udisks::Model& model);

private:
    uint64_t minimumSizeOf(const Slice& slice) const;
    // The lowest and highest end of slice index, as its own limits allow.
    std::pair<uint64_t, uint64_t> endRange(size_t index) const;
    size_t partitionCount() const;
    // A partition's size as found, taking back the space after it.
    void restoreSize(size_t index);
    uint32_t insert(size_t index, Slice slice);

    std::string m_disk;
    std::string m_diskName;
    std::string m_tableType;
    bool m_createsTable = false;
    uint64_t m_start = 0;
    uint64_t m_end = 0;
    std::vector<Slice> m_slices;
    // Deleted partitions, as they were.
    std::vector<Slice> m_deleted;
    std::map<std::string, ResizeSupport> m_resize;
    uint32_t m_nextId = 1;
    // Teardown steps for the mounted and unlocked volumes, by block, for
    // partitions that are reformatted.
    std::map<std::string, std::vector<PlanStep>> m_teardown;
};

// Unmounts the block's filesystem (its cleartext block's for an unlocked
// encrypted block) and locks it: what erasing it needs first.
std::vector<PlanStep> teardownSteps(const udisks::Model& model, const udisks::Object& block,
                                    const std::string& subject);

} // namespace dialogs
