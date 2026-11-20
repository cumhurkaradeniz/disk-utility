// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "fake_udisks.h"

#include "udisks/model.h"
#include "udisks/tree.h"

#include <gtest/gtest.h>

using namespace udisks;
using namespace udisks::fake;

static std::vector<std::string> ids(const std::vector<Node>& nodes)
{
    std::vector<std::string> result;
    for (const Node& node : nodes)
        result.push_back(node.id);
    return result;
}

static std::string volume(const std::string& block)
{
    return "v:" + block;
}

static std::string disk(const std::string& block)
{
    return "d:" + block;
}

TEST(TreeTest, OnlyVolumes)
{
    Model model;
    loadSample(model);
    const std::vector<SectionNodes> tree = buildTree(model, ViewMode::Volumes);
    ASSERT_EQ(tree.size(), 3u);

    EXPECT_EQ(tree[0].section, Section::Internal);
    // The EFI partition is hinted to be ignored.
    EXPECT_EQ(ids(tree[0].nodes), (std::vector<std::string>{volume(sample::kRoot), volume(sample::kData)}));

    EXPECT_EQ(tree[1].section, Section::External);
    // By UDisks2's sort key; a disk without volumes stands for itself, an
    // unformatted partition and an extended one are left out.
    EXPECT_EQ(ids(tree[1].nodes),
              (std::vector<std::string>{volume(sample::kStickVolume), volume(sample::kUnlocked),
                                        volume(sample::kLocked), disk(sample::kCard), volume(sample::kMbrLinux),
                                        volume(sample::kLogicalFat)}));

    EXPECT_EQ(tree[2].section, Section::Images);
    // The unused loop device and the compressed swap are not shown.
    EXPECT_EQ(ids(tree[2].nodes), std::vector<std::string>{volume(sample::kImage)});

    for (const SectionNodes& section : tree) {
        for (const Node& node : section.nodes)
            EXPECT_TRUE(node.children.empty()) << node.id;
    }
}

TEST(TreeTest, AllDevices)
{
    Model model;
    loadSample(model);
    const std::vector<SectionNodes> tree = buildTree(model, ViewMode::AllDevices);
    ASSERT_EQ(tree.size(), 3u);

    EXPECT_EQ(ids(tree[0].nodes), (std::vector<std::string>{disk(sample::kNvme), disk(sample::kSata)}));
    EXPECT_EQ(ids(tree[0].nodes[0].children),
              (std::vector<std::string>{volume(sample::kEfi), volume(sample::kRoot)}));

    ASSERT_EQ(ids(tree[1].nodes), (std::vector<std::string>{disk(sample::kStick), disk(sample::kUsb),
                                                            disk(sample::kCard), disk(sample::kMbr)}));
    EXPECT_EQ(ids(tree[1].nodes[1].children),
              (std::vector<std::string>{volume(sample::kUnlocked), volume(sample::kLocked),
                                        "p:" + sample::kUnformatted}));
    EXPECT_TRUE(tree[1].nodes[2].children.empty());
    const Node& mbr = tree[1].nodes[3];
    ASSERT_EQ(ids(mbr.children), (std::vector<std::string>{volume(sample::kMbrLinux), "c:" + sample::kExtended}));
    EXPECT_EQ(mbr.children[1].kind, NodeKind::Container);
    EXPECT_EQ(ids(mbr.children[1].children),
              (std::vector<std::string>{volume(sample::kLogicalFat), "p:" + sample::kLogicalSwap}));

    // A disk image without a partition table: the disk, and its filesystem
    // as the volume on it.
    ASSERT_EQ(ids(tree[2].nodes), std::vector<std::string>{disk(sample::kImage)});
    EXPECT_EQ(ids(tree[2].nodes[0].children), std::vector<std::string>{volume(sample::kImage)});
}

TEST(TreeTest, NodeFields)
{
    Model model;
    loadSample(model);
    const std::vector<SectionNodes> tree = buildTree(model, ViewMode::AllDevices);

    const Node* unlocked = findNode(tree, volume(sample::kUnlocked));
    ASSERT_NE(unlocked, nullptr);
    EXPECT_EQ(unlocked->kind, NodeKind::Volume);
    EXPECT_EQ(unlocked->block, sample::kUnlocked);
    EXPECT_EQ(unlocked->drive, sample::kUsbDrive);
    EXPECT_EQ(unlocked->disk, sample::kUsb);

    const Node* usb = findNode(tree, disk(sample::kUsb));
    ASSERT_NE(usb, nullptr);
    EXPECT_EQ(usb->kind, NodeKind::Disk);
    EXPECT_EQ(usb->block, sample::kUsb);
    EXPECT_EQ(usb->disk, sample::kUsb);

    const Node* image = findNode(tree, volume(sample::kImage));
    ASSERT_NE(image, nullptr);
    EXPECT_TRUE(image->drive.empty());
    EXPECT_EQ(image->disk, sample::kImage);

    const Node* logical = findNode(tree, volume(sample::kLogicalFat));
    ASSERT_NE(logical, nullptr);
    EXPECT_EQ(logical->disk, sample::kMbr);
    EXPECT_EQ(findNode(tree, "v:/nowhere"), nullptr);

    // The same in the other view.
    const std::vector<SectionNodes> volumes = buildTree(model, ViewMode::Volumes);
    ASSERT_NE(findNode(volumes, volume(sample::kUnlocked)), nullptr);
    EXPECT_EQ(*findNode(volumes, volume(sample::kUnlocked)), *unlocked);
}

TEST(TreeTest, SectionsOfDrives)
{
    Model model;
    loadSample(model);
    EXPECT_EQ(sectionOfDrive(model, sample::kNvmeDrive), Section::Internal);
    EXPECT_EQ(sectionOfDrive(model, sample::kSataDrive), Section::Internal);
    EXPECT_EQ(sectionOfDrive(model, sample::kStickDrive), Section::External);
    // On USB without being removable.
    EXPECT_EQ(sectionOfDrive(model, sample::kUsbDrive), Section::External);
    EXPECT_EQ(sectionOfDrive(model, sample::kCardDrive), Section::External);
}

TEST(TreeTest, EmptySectionsAreLeftOut)
{
    Model model;
    Model::Objects objects = sampleTree();
    std::erase_if(objects, [](const auto& entry) {
        return entry.first != sample::kNvmeDrive && entry.first != sample::kNvme && entry.first != sample::kEfi
                && entry.first != sample::kRoot;
    });
    model.reset(objects);
    const std::vector<SectionNodes> tree = buildTree(model, ViewMode::Volumes);
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].section, Section::Internal);

    model.reset({});
    EXPECT_TRUE(buildTree(model, ViewMode::AllDevices).empty());
}

TEST(TreeTest, FollowsTheModel)
{
    Model model;
    loadSample(model);

    // A card put into the reader gets a filesystem.
    model.changeProperties(sample::kCard, interface::kBlock,
                           {{"IdUsage", Variant(std::string("filesystem"))},
                            {"IdType", Variant(std::string("exfat"))},
                            {"IdLabel", Variant(std::string("CARD"))}});
    model.addInterfaces(sample::kCard, {{interface::kFilesystem, {{"MountPoints",
                                                                    Variant(std::vector<std::vector<uint8_t>>())}}}});
    std::vector<SectionNodes> tree = buildTree(model, ViewMode::Volumes);
    EXPECT_NE(findNode(tree, volume(sample::kCard)), nullptr);
    EXPECT_EQ(findNode(tree, disk(sample::kCard)), nullptr);

    // The stick is pulled out.
    model.removeInterfaces(sample::kStickVolume, {interface::kBlock, interface::kPartition, interface::kFilesystem});
    model.removeInterfaces(sample::kStick, {interface::kBlock, interface::kPartitionTable});
    model.removeInterfaces(sample::kStickDrive, {interface::kDrive});
    tree = buildTree(model, ViewMode::AllDevices);
    EXPECT_EQ(findNode(tree, disk(sample::kStick)), nullptr);
    EXPECT_EQ(findNode(tree, volume(sample::kStickVolume)), nullptr);

    // A drive without its medium is not listed.
    model.changeProperties(sample::kCard, interface::kBlock, {{"Size", Variant(uint64_t(0))}});
    tree = buildTree(model, ViewMode::AllDevices);
    EXPECT_EQ(findNode(tree, disk(sample::kCard)), nullptr);
}
