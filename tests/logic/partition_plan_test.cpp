// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "fake_udisks.h"

#include "actions.h"
#include "dialogs/partition_plan.h"
#include "dialogs/volume_format.h"
#include "udisks/model.h"

#include <gtest/gtest.h>

#include <sys/sysmacros.h>

using namespace dialogs;
using namespace udisks::fake;
using Kind = dialogs::PlanStep::Kind;


// No filesystem reports its usage: nothing is mounted for real.
static std::optional<udisks::FilesystemStats> noStats(const std::string&)
{
    return std::nullopt;
}

class PartitionPlanTest : public testing::Test
{
protected:
    void SetUp() override { loadSample(model); }

    PartitionLayout layoutOf(const std::string& disk) { return PartitionLayout::fromDisk(model, disk, noStats); }

    static std::vector<Kind> kinds(const std::vector<PlanStep>& steps)
    {
        std::vector<Kind> result;
        for (const PlanStep& step : steps)
            result.push_back(step.kind);
        return result;
    }

    static uint32_t idOf(const PartitionLayout& layout, const std::string& block)
    {
        for (const Slice& slice : layout.slices()) {
            if (slice.block == block)
                return slice.id;
        }
        ADD_FAILURE() << "no slice of " << block;
        return 0;
    }

    udisks::Model model;
};

TEST_F(PartitionPlanTest, FreeRegionsAgreeWithAddVolume)
{
    for (const std::string& disk : {sample::kSata, sample::kUsb, sample::kMbr, sample::kStick, sample::kNvme}) {
        uint64_t largest = 0;
        for (const FreeRegion& region : freeRegions(model, disk)) {
            EXPECT_EQ(region.offset % kMiB, 0u);
            EXPECT_EQ(region.size % kMiB, 0u);
            largest = std::max(largest, region.size);
        }
        EXPECT_EQ(largest, largestFreeSpace(model, disk)) << disk;
    }
    EXPECT_TRUE(freeRegions(model, sample::kCard).empty());
}

TEST_F(PartitionPlanTest, UsableSpace)
{
    EXPECT_EQ(usableStart(), kMiB);
    EXPECT_EQ(usableEnd(100 * kMiB, "gpt"), 99 * kMiB);
    // An MBR stops at 2 TiB.
    EXPECT_EQ(usableEnd(uint64_t(4) << 40, "dos"), uint64_t(2) << 40);
    EXPECT_EQ(usableEnd(uint64_t(4) << 40, "gpt"), (uint64_t(4) << 40) - kMiB);
    EXPECT_EQ(maximumPartitions("dos"), 4u);
    EXPECT_EQ(maximumPartitions("gpt"), 128u);
}

TEST_F(PartitionPlanTest, ReadsTheDisk)
{
    const PartitionLayout layout = layoutOf(sample::kSata);
    EXPECT_EQ(layout.tableType(), "gpt");
    EXPECT_FALSE(layout.createsTable());
    ASSERT_EQ(layout.slices().size(), 2u);
    const Slice& data = layout.slices()[0];
    EXPECT_EQ(data.kind, Slice::Kind::Partition);
    EXPECT_EQ(data.block, sample::kData);
    EXPECT_EQ(data.name, "Data");
    EXPECT_EQ(data.filesystem, "ntfs");
    EXPECT_EQ(data.size, 500 * kGB);
    EXPECT_FALSE(data.mounted);
    const Slice& free = layout.slices()[1];
    EXPECT_EQ(free.kind, Slice::Kind::Free);
    EXPECT_EQ(free.offset, alignUp(kMiB + 500 * kGB));
    EXPECT_EQ(free.end(), layout.end());
    EXPECT_FALSE(layout.isChanged());
    EXPECT_TRUE(layout.plan().empty());
}

TEST_F(PartitionPlanTest, EncryptedPartitionsShowTheirContent)
{
    PartitionLayout layout = layoutOf(sample::kUsb);
    const Slice* unlocked = layout.slice(idOf(layout, sample::kUnlocked));
    ASSERT_TRUE(unlocked);
    EXPECT_TRUE(unlocked->encrypted);
    EXPECT_TRUE(unlocked->mounted);
    EXPECT_EQ(unlocked->contentBlock, sample::kCleartext);
    EXPECT_EQ(unlocked->name, "Secret");
    EXPECT_EQ(unlocked->filesystem, "ext4");
    layout.setResizeSupport("ext4", true, 2 | 4 | 16, "");
    const ActionState shrink = layout.shrinkState(unlocked->id);
    EXPECT_FALSE(shrink.enabled);
    EXPECT_NE(shrink.reason.find("Encrypted"), std::string::npos);
    EXPECT_EQ(layout.resizableFilesystems(), std::vector<std::string>{});
}

TEST_F(PartitionPlanTest, AddsInTheFreeSpace)
{
    PartitionLayout layout = layoutOf(sample::kSata);
    const uint64_t freeOffset = layout.slices()[1].offset;
    const uint64_t freeSize = layout.slices()[1].size;
    ASSERT_TRUE(layout.addState(std::nullopt).enabled);
    const std::optional<uint32_t> id = layout.add(std::nullopt, "exfat", "Backup");
    ASSERT_TRUE(id);
    const Slice* created = layout.slice(*id);
    ASSERT_TRUE(created);
    EXPECT_EQ(created->kind, Slice::Kind::New);
    EXPECT_EQ(created->offset, freeOffset);
    EXPECT_EQ(created->size, freeSize);
    EXPECT_TRUE(layout.isChanged());
    EXPECT_EQ(layout.problem(), "");

    const std::vector<PlanStep> steps = layout.plan();
    ASSERT_EQ(kinds(steps), std::vector<Kind>{Kind::Create});
    EXPECT_EQ(steps[0].path, sample::kSata);
    EXPECT_EQ(steps[0].offset, freeOffset);
    EXPECT_EQ(steps[0].size, freeSize);
    EXPECT_EQ(steps[0].type, "exfat");
    EXPECT_EQ(steps[0].partitionName, "Backup");
    EXPECT_EQ(steps[0].options.label, "Backup");
    EXPECT_FALSE(steps[0].options.takeOwnership);
}

TEST_F(PartitionPlanTest, SplitsTheSelectionWithoutFreeSpace)
{
    PartitionLayout layout = layoutOf(sample::kSata);
    const std::optional<uint32_t> first = layout.add(std::nullopt, "ext4", "One");
    ASSERT_TRUE(first);
    const uint64_t whole = layout.slice(*first)->size;
    // No free space is left: + halves the selected new partition.
    ASSERT_TRUE(layout.addState(first).enabled);
    EXPECT_FALSE(layout.addState(std::nullopt).enabled);
    const std::optional<uint32_t> second = layout.add(first, "ext4", "Two");
    ASSERT_TRUE(second);
    EXPECT_EQ(layout.slice(*first)->size, alignDown(whole / 2));
    EXPECT_EQ(layout.slice(*second)->offset, layout.slice(*first)->end());
    EXPECT_EQ(layout.slice(*second)->end(), layout.slice(*first)->offset + whole);
    // The NTFS volume itself cannot be split until its resizing is known.
    EXPECT_FALSE(layout.addState(idOf(layout, sample::kData)).enabled);
}

TEST_F(PartitionPlanTest, BoundaryBetweenNewPartitions)
{
    PartitionLayout layout = layoutOf(sample::kSata);
    const uint32_t first = *layout.add(std::nullopt, "ext4", "One");
    const uint32_t second = *layout.add(first, "xfs", "Two");
    const size_t index = *layout.indexOf(first);
    ASSERT_TRUE(layout.canMoveBoundary(index));
    const auto [low, high] = layout.boundaryRange(index);
    EXPECT_EQ(low, layout.slice(first)->offset + minimumSize("ext4"));
    EXPECT_EQ(high, layout.slice(second)->end() - minimumSize("xfs"));

    // Dragged past the end: the second keeps what XFS needs.
    layout.moveBoundary(index, layout.end());
    EXPECT_EQ(layout.slice(second)->size, minimumSize("xfs"));
    EXPECT_EQ(layout.slice(first)->end(), layout.slice(second)->offset);
    // To a MiB.
    layout.moveBoundary(index, layout.slice(first)->offset + 10 * kGB + 123);
    EXPECT_EQ(layout.slice(first)->size % kMiB, 0u);
    EXPECT_EQ(layout.slice(first)->end() % kMiB, 0u);
    EXPECT_EQ(layout.problem(), "");
}

TEST_F(PartitionPlanTest, ShrinkingNeedsTheFilesystemsTools)
{
    PartitionLayout layout = layoutOf(sample::kSata);
    const uint32_t data = idOf(layout, sample::kData);
    EXPECT_EQ(layout.resizableFilesystems(), std::vector<std::string>{"ntfs"});
    EXPECT_FALSE(layout.shrinkState(data).enabled);
    EXPECT_FALSE(layout.canMoveBoundary(0));

    layout.setResizeSupport("ntfs", false, 0, "ntfsresize");
    EXPECT_NE(layout.shrinkState(data).reason.find("ntfsresize"), std::string::npos);

    layout.setResizeSupport("ntfs", true, 2 | 4, "");
    EXPECT_TRUE(layout.shrinkState(data).enabled);
    EXPECT_TRUE(layout.growState(data).enabled);
    layout.setSize(data, 300 * kGB);
    const Slice* shrunk = layout.slice(data);
    // Its end on the nearest MiB.
    EXPECT_EQ(shrunk->end(), (kMiB + 300 * kGB + kMiB / 2) / kMiB * kMiB);
    // The space after it joined the free space.
    ASSERT_EQ(layout.slices().size(), 2u);
    EXPECT_EQ(layout.slices()[1].offset, shrunk->end());

    const std::vector<PlanStep> steps = layout.plan();
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::ShrinkFilesystem, Kind::ResizePartition}));
    EXPECT_EQ(steps[0].path, sample::kData);
    EXPECT_EQ(steps[0].size, shrunk->size);
    EXPECT_EQ(steps[1].path, sample::kData);
    EXPECT_EQ(steps[1].size, shrunk->size);
}

TEST_F(PartitionPlanTest, GrowingResizesThePartitionFirst)
{
    PartitionLayout layout = layoutOf(sample::kSata);
    layout.setResizeSupport("ntfs", true, 2 | 4, "");
    const uint32_t data = idOf(layout, sample::kData);
    layout.moveBoundary(0, layout.end());
    layout.normalize();
    ASSERT_EQ(layout.slices().size(), 1u);
    EXPECT_EQ(layout.slice(data)->end(), layout.end());
    EXPECT_EQ(kinds(layout.plan()), (std::vector<Kind>{Kind::ResizePartition, Kind::GrowFilesystem}));
}

TEST_F(PartitionPlanTest, MountedVolumesNeedOnlineResizing)
{
    PartitionLayout layout = layoutOf(sample::kStick);
    const uint32_t stick = idOf(layout, sample::kStickVolume);
    layout.setResizeSupport("vfat", true, 2 | 4, "");
    const ActionState offline = layout.shrinkState(stick);
    EXPECT_FALSE(offline.enabled);
    EXPECT_EQ(offline.reason, "Unmount “STICK” to shrink it.");

    layout.setResizeSupport("vfat", true, 8 | 16, "");
    EXPECT_TRUE(layout.shrinkState(stick).enabled);

    layout.setResizeSupport("vfat", true, 4, "");
    EXPECT_EQ(layout.shrinkState(stick).reason, "FAT32 (MS-DOS) can't shrink.");
}

TEST_F(PartitionPlanTest, DeletingFreesTheSpace)
{
    PartitionLayout layout = layoutOf(sample::kSata);
    const uint32_t data = idOf(layout, sample::kData);
    ASSERT_TRUE(layout.removeState(data).enabled);
    ASSERT_TRUE(layout.remove(data));
    ASSERT_EQ(layout.slices().size(), 1u);
    EXPECT_EQ(layout.slices()[0].kind, Slice::Kind::Free);
    EXPECT_EQ(layout.slices()[0].offset, kMiB);

    const uint32_t created = *layout.add(std::nullopt, "ext4", "Linux");
    EXPECT_EQ(layout.slice(created)->offset, kMiB);
    const std::vector<PlanStep> steps = layout.plan();
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::Delete, Kind::Create}));
    EXPECT_EQ(steps[0].path, sample::kData);
    EXPECT_EQ(steps[0].subject, "Data");
    EXPECT_TRUE(steps[1].options.takeOwnership);

    // A new partition taken back leaves nothing to do but the delete.
    ASSERT_TRUE(layout.remove(created));
    EXPECT_EQ(kinds(layout.plan()), std::vector<Kind>{Kind::Delete});
}

TEST_F(PartitionPlanTest, StepsInASafeOrder)
{
    PartitionLayout layout = layoutOf(sample::kUsb);
    layout.setResizeSupport("ext4", true, 2 | 4 | 16, "");
    const uint32_t locked = idOf(layout, sample::kLocked);
    const uint32_t unformatted = idOf(layout, sample::kUnformatted);
    // Renamed, erased, deleted and added, in the opposite order.
    layout.add(std::nullopt, "vfat", "Photos");
    layout.setFilesystem(unformatted, "ext4");
    layout.setName(unformatted, "Scratch");
    ASSERT_TRUE(layout.remove(locked));
    layout.setName(idOf(layout, sample::kUnlocked), "Private");

    const std::vector<PlanStep> steps = layout.plan();
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::Delete, Kind::Format, Kind::Create, Kind::SetLabel}));
    EXPECT_EQ(steps[0].path, sample::kLocked);
    EXPECT_EQ(steps[1].path, sample::kUnformatted);
    EXPECT_EQ(steps[1].type, "ext4");
    EXPECT_EQ(steps[1].options.label, "Scratch");
    EXPECT_EQ(steps[2].type, "vfat");
    EXPECT_EQ(steps[2].options.label, "PHOTOS");
    EXPECT_EQ(steps[2].partitionName, "PHOTOS");
    // The cleartext block holds the name.
    EXPECT_EQ(steps[3].path, sample::kCleartext);
    EXPECT_EQ(steps[3].options.label, "Private");
}

TEST_F(PartitionPlanTest, ReformattingTearsDownFirst)
{
    PartitionLayout layout = layoutOf(sample::kStick);
    const uint32_t stick = idOf(layout, sample::kStickVolume);
    layout.setFilesystem(stick, "exfat");
    EXPECT_TRUE(layout.slice(stick)->reformats());
    // The content goes, so the partition alone is resized.
    EXPECT_TRUE(layout.shrinkState(stick).enabled);
    layout.setSize(stick, 8 * kGB);
    const std::vector<PlanStep> steps = layout.plan();
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::Unmount, Kind::ResizePartition, Kind::Format}));
    EXPECT_EQ(steps[0].path, sample::kStickVolume);
    EXPECT_EQ(steps[2].type, "exfat");
    EXPECT_EQ(steps[2].options.label, "STICK");

    PartitionLayout usb = layoutOf(sample::kUsb);
    usb.setFilesystem(idOf(usb, sample::kUnlocked), "ntfs");
    EXPECT_EQ(kinds(usb.plan()), (std::vector<Kind>{Kind::Unmount, Kind::Lock, Kind::Format}));
    EXPECT_EQ(usb.plan()[0].path, sample::kCleartext);
    EXPECT_EQ(usb.plan()[1].path, sample::kUnlocked);
}

TEST_F(PartitionPlanTest, MasterBootRecordHoldsFourPartitions)
{
    PartitionLayout layout = layoutOf(sample::kMbr);
    ASSERT_EQ(layout.slices().size(), 3u);
    const Slice& extended = layout.slices()[1];
    EXPECT_EQ(extended.kind, Slice::Kind::Container);
    EXPECT_FALSE(layout.removeState(extended.id).enabled);
    EXPECT_FALSE(layout.removeState(extended.id).reason.empty());
    EXPECT_FALSE(layout.canMoveBoundary(1));

    const uint32_t third = *layout.add(std::nullopt, "ext4", "Three");
    ASSERT_TRUE(layout.add(third, "ext4", "Four"));
    const ActionState full = layout.addState(third);
    EXPECT_FALSE(full.enabled);
    EXPECT_EQ(full.reason, "A Master Boot Record holds at most four partitions.");
    // No partition names on MBR.
    for (const PlanStep& step : layout.plan())
        EXPECT_EQ(step.partitionName, "");
}

TEST_F(PartitionPlanTest, DiskWithoutAPartitionMap)
{
    PartitionLayout layout = layoutOf(sample::kCard);
    EXPECT_TRUE(layout.createsTable());
    EXPECT_EQ(layout.tableType(), "gpt");
    ASSERT_EQ(layout.slices().size(), 1u);
    EXPECT_EQ(layout.slices()[0].kind, Slice::Kind::Free);
    EXPECT_TRUE(layout.plan().empty());

    layout.add(std::nullopt, "exfat", "Card");
    const std::vector<PlanStep> steps = layout.plan();
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::CreateTable, Kind::Create}));
    EXPECT_EQ(steps[0].path, sample::kCard);
    EXPECT_EQ(steps[0].type, "gpt");
    EXPECT_EQ(steps[1].path, sample::kCard);
    EXPECT_EQ(steps[1].offset, kMiB);
}

TEST_F(PartitionPlanTest, Problems)
{
    PartitionLayout layout = layoutOf(sample::kSata);
    const uint32_t first = *layout.add(std::nullopt, "ext4", "One");
    layout.setName(first, "a name far too long for ext4");
    EXPECT_NE(layout.problem().find("16 bytes"), std::string::npos);
    layout.setName(first, "One");
    EXPECT_EQ(layout.problem(), "");

    // A size typed below the filesystem's minimum stops at it.
    layout.setSize(first, kMiB);
    EXPECT_EQ(layout.slice(first)->size, minimumSize("ext4"));
    // A format chosen after the size can be too big for it.
    layout.setFilesystem(first, "xfs");
    EXPECT_NE(layout.problem().find("too small"), std::string::npos);
}

TEST_F(PartitionPlanTest, TeardownSteps)
{
    const udisks::Object* stick = model.object(sample::kStickVolume);
    std::vector<PlanStep> steps = teardownSteps(model, *stick, "STICK");
    ASSERT_EQ(kinds(steps), std::vector<Kind>{Kind::Unmount});
    EXPECT_EQ(steps[0].path, sample::kStickVolume);

    steps = teardownSteps(model, *model.object(sample::kUnlocked), "Secret");
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::Unmount, Kind::Lock}));
    EXPECT_EQ(steps[0].path, sample::kCleartext);
    EXPECT_EQ(steps[1].path, sample::kUnlocked);

    EXPECT_TRUE(teardownSteps(model, *model.object(sample::kLocked), "x").empty());
    EXPECT_TRUE(teardownSteps(model, *model.object(sample::kData), "Data").empty());
}

TEST_F(PartitionPlanTest, AFormatTakenBackTakesItsSizeBack)
{
    // An encrypted volume may change size only while it is erased.
    PartitionLayout usb = layoutOf(sample::kUsb);
    usb.setResizeSupport("ext4", true, 2 | 4 | 16, "");
    const uint32_t unlocked = idOf(usb, sample::kUnlocked);
    usb.setFilesystem(unlocked, "ntfs");
    usb.setSize(unlocked, 100 * kGB);
    ASSERT_LT(usb.slice(unlocked)->size, usb.slice(unlocked)->originalSize);
    usb.setFilesystem(unlocked, "ext4");
    EXPECT_EQ(usb.slice(unlocked)->size, usb.slice(unlocked)->originalSize);
    EXPECT_EQ(usb.problem(), "");
    // Nothing shrinks the cleartext filesystem past the LUKS header.
    EXPECT_TRUE(usb.plan().empty());
    EXPECT_EQ(usb.slices()[1].offset, usb.slice(unlocked)->end());

    // Grown while erased, then the format taken back: the free space returns.
    PartitionLayout sata = layoutOf(sample::kSata);
    const uint32_t data = idOf(sata, sample::kData);
    sata.setFilesystem(data, "ext4");
    sata.moveBoundary(0, sata.end());
    sata.normalize();
    ASSERT_EQ(sata.slices().size(), 1u);
    sata.setFilesystem(data, "ntfs");
    EXPECT_EQ(sata.slice(data)->size, 500 * kGB);
    ASSERT_EQ(sata.slices().size(), 2u);
    EXPECT_EQ(sata.slices()[1].kind, Slice::Kind::Free);
    EXPECT_TRUE(sata.plan().empty());
}

TEST_F(PartitionPlanTest, ASizeNoLongerAllowedStopsThePlan)
{
    PartitionLayout layout = layoutOf(sample::kSata);
    const uint32_t data = idOf(layout, sample::kData);
    layout.setResizeSupport("ntfs", true, 2 | 4, "");
    layout.setSize(data, 300 * kGB);
    ASSERT_FALSE(layout.plan().empty());
    layout.setResizeSupport("ntfs", false, 0, "ntfsresize");
    EXPECT_NE(layout.problem().find("ntfsresize"), std::string::npos);
    EXPECT_TRUE(layout.plan().empty());
}

// A disk of two ext4 partitions, and another of the given size and table.
static udisks::Model::Objects twoPartitions(uint64_t diskSize, const std::string& table, int partitions)
{
    TreeBuilder tree;
    const std::string drive = tree.addDrive("Spare", {.model = "Spare", .size = diskSize, .sortKey = "02"});
    const std::string disk = tree.addDisk("sdx", drive, {.size = diskSize, .deviceNumber = makedev(8, 96)}, table);
    for (int i = 0; i < partitions; ++i) {
        const std::string name = "sdx" + std::to_string(i + 1);
        const std::string block = tree.addPartition(
            disk, name, uint32_t(i + 1), kMiB + uint64_t(i) * 40 * kGB,
            {.size = 40 * kGB, .deviceNumber = makedev(8, 97 + i), .idUsage = "filesystem", .idType = "ext4",
             .idLabel = "P" + std::to_string(i + 1)});
        tree.setFilesystem(block, {});
    }
    return tree.objects();
}

TEST_F(PartitionPlanTest, ShrinksRunBeforeDeletes)
{
    model.reset(twoPartitions(100 * kGB, "gpt", 2));
    const std::string disk = TreeBuilder::blockPath("sdx");
    PartitionLayout layout = layoutOf(disk);
    layout.setResizeSupport("ext4", true, 2 | 4 | 16, "");
    const uint32_t first = idOf(layout, TreeBuilder::blockPath("sdx1"));
    const uint32_t second = idOf(layout, TreeBuilder::blockPath("sdx2"));
    ASSERT_TRUE(layout.remove(first));
    layout.setSize(second, 20 * kGB);
    const std::vector<PlanStep> steps = layout.plan();
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::ShrinkFilesystem, Kind::ResizePartition, Kind::Delete}));
    EXPECT_EQ(steps[2].path, TreeBuilder::blockPath("sdx1"));
}

TEST_F(PartitionPlanTest, MasterBootRecordStopsAtTwoTiB)
{
    const uint64_t threeTB = 3000 * kGB;
    model.reset(twoPartitions(threeTB, "dos", 1));
    const std::string disk = TreeBuilder::blockPath("sdx");
    const uint64_t twoTiB = uint64_t(1) << 41;
    EXPECT_EQ(largestFreeSpace(model, disk), twoTiB - alignUp(kMiB + 40 * kGB));
    ASSERT_EQ(freeRegions(model, disk).size(), 1u);
    EXPECT_EQ(freeRegions(model, disk)[0].offset + freeRegions(model, disk)[0].size, twoTiB);
    EXPECT_EQ(layoutOf(disk).end(), twoTiB);

    // Four primary partitions leave no room for Add Partition, with why.
    model.reset(twoPartitions(threeTB, "dos", 4));
    udisks::Node node;
    node.kind = udisks::NodeKind::Volume;
    node.block = TreeBuilder::blockPath("sdx1");
    node.disk = disk;
    node.drive = TreeBuilder::drivePath("Spare");
    const ActionState state = actionState(Action::AddVolume, model, nullptr, node);
    EXPECT_FALSE(state.enabled);
    EXPECT_EQ(state.reason, "A Master Boot Record holds at most four partitions.");
}

TEST_F(PartitionPlanTest, TeardownIsReadAtApply)
{
    PartitionLayout layout = layoutOf(sample::kStick);
    const uint32_t stick = idOf(layout, sample::kStickVolume);
    layout.setFilesystem(stick, "exfat");
    ASSERT_EQ(kinds(layout.plan()), (std::vector<Kind>{Kind::Unmount, Kind::Format}));
    // Unmounted since the editor opened.
    model.changeProperties(sample::kStickVolume, udisks::interface::kFilesystem,
                           {{"MountPoints", udisks::Variant(std::vector<std::vector<uint8_t>>())}});
    layout.updateTeardown(model);
    EXPECT_EQ(kinds(layout.plan()), std::vector<Kind>{Kind::Format});
}

TEST_F(PartitionPlanTest, NamesOnlyWhereAFilesystemIs)
{
    PartitionLayout layout = layoutOf(sample::kMbr);
    // The swap partition inside the extended one is not listed; on the USB
    // disk, an unformatted partition has no filesystem to rename.
    PartitionLayout usb = layoutOf(sample::kUsb);
    const uint32_t unformatted = idOf(usb, sample::kUnformatted);
    EXPECT_FALSE(usb.slice(unformatted)->hasFilesystem);
    EXPECT_TRUE(usb.slice(idOf(usb, sample::kUnlocked))->hasFilesystem);
    EXPECT_FALSE(usb.slice(idOf(usb, sample::kLocked))->hasFilesystem);
    usb.setName(unformatted, "Nope");
    EXPECT_TRUE(usb.plan().empty());
    EXPECT_TRUE(layout.slice(idOf(layout, sample::kMbrLinux))->hasFilesystem);
}
