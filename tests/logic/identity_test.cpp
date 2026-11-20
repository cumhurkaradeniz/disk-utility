// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "fake_udisks.h"

#include "udisks/identity.h"
#include "udisks/model.h"

#include <gtest/gtest.h>

#include <sys/sysmacros.h>

using namespace udisks;
using namespace udisks::fake;

static constexpr uint64_t kMiB = 1024 * 1024;

// The stick pulled out and another put in: UDisks2 gives the new one the
// same paths.
static void replugAnotherStick(Model& model)
{
    model.removeInterfaces(sample::kStickVolume, {interface::kBlock, interface::kPartition, interface::kFilesystem});
    model.removeInterfaces(sample::kStick, {interface::kBlock, interface::kPartitionTable});
    model.removeInterfaces(sample::kStickDrive, {interface::kDrive});
    TreeBuilder other;
    const std::string drive = other.addDrive("SanDisk_Cruzer",
                                             {.vendor = "SanDisk", .model = "Cruzer Blade", .serial = "9999",
                                              .size = 16008609792, .connectionBus = "usb", .removable = true});
    const std::string disk = other.addDisk("sdb", drive, {.size = 16008609792, .deviceNumber = makedev(8, 16)}, "dos");
    const std::string volume = other.addPartition(disk, "sdb1", 1, kMiB,
                                                  {.size = 16008609792 - kMiB, .deviceNumber = makedev(8, 17),
                                                   .idUsage = "filesystem", .idType = "vfat", .idLabel = "OTHER"},
                                                  "0x0c");
    for (const std::string& path : {drive, disk, volume})
        model.addInterfaces(path, other.objects().at(path));
}

TEST(IdentityTest, TheSameBlockMatches)
{
    Model model;
    loadSample(model);
    for (const std::string& block : {sample::kStick, sample::kStickVolume, sample::kUnlocked, sample::kCleartext,
                                     sample::kImage, sample::kLogicalFat}) {
        const Identity identity = identityOf(model, block);
        ASSERT_TRUE(identity.isValid()) << block;
        EXPECT_TRUE(model.matches(identity)) << block;
    }
    EXPECT_FALSE(identityOf(model, "/nowhere").isValid());
    EXPECT_FALSE(model.matches(Identity()));
}

TEST(IdentityTest, AnotherStickAtThePath)
{
    Model model;
    loadSample(model);
    const Identity disk = identityOf(model, sample::kStick);
    const Identity volume = identityOf(model, sample::kStickVolume);
    replugAnotherStick(model);
    ASSERT_NE(model.object(sample::kStickVolume), nullptr);
    EXPECT_FALSE(model.matches(disk));
    EXPECT_FALSE(model.matches(volume));
}

TEST(IdentityTest, WhatAnOperationChangesKeepsIt)
{
    Model model;
    loadSample(model);
    const Identity volume = identityOf(model, sample::kStickVolume);
    const Identity disk = diskIdentityOf(model, sample::kStickVolume);
    EXPECT_EQ(disk.block, sample::kStick);
    model.changeProperties(sample::kStickVolume, interface::kBlock,
                           {{"IdLabel", Variant(std::string("RENAMED"))}, {"IdUUID", Variant(std::string("NEW"))},
                            {"IdType", Variant(std::string("exfat"))}});
    model.changeProperties(sample::kStickVolume, interface::kFilesystem,
                           {{"MountPoints", Variant(std::vector<std::vector<uint8_t>>())}});
    EXPECT_TRUE(model.matches(volume));

    // A partition moved or resized is another one; its disk stays.
    model.changeProperties(sample::kStickVolume, interface::kPartition, {{"Size", Variant(uint64_t(8 * kMiB))}});
    model.changeProperties(sample::kStickVolume, interface::kBlock, {{"Size", Variant(uint64_t(8 * kMiB))}});
    EXPECT_FALSE(model.matches(volume));
    EXPECT_TRUE(model.matches(disk));
}

TEST(IdentityTest, AnotherImageOnTheLoopDevice)
{
    Model model;
    loadSample(model);
    const Identity image = identityOf(model, sample::kImage);
    model.changeProperties(sample::kImage, interface::kLoop,
                           {{"BackingFile", Variant(encodeBytes("/home/u/Downloads/other.iso"))}});
    EXPECT_FALSE(model.matches(image));
}

TEST(IdentityTest, TheWatchSaysOnceWhenItGoes)
{
    Model model;
    loadSample(model);
    int lost = 0;
    IdentityWatch watch(model, {identityOf(model, sample::kStickVolume), identityOf(model, sample::kData)},
                        [&] { ++lost; });
    model.changeProperties(sample::kData, interface::kBlock, {{"IdLabel", Variant(std::string("Archive"))}});
    EXPECT_EQ(lost, 0);
    EXPECT_TRUE(watch.holds());
    replugAnotherStick(model);
    EXPECT_EQ(lost, 1);
    EXPECT_FALSE(watch.holds());
    model.changeProperties(sample::kData, interface::kBlock, {{"IdLabel", Variant(std::string("Again"))}});
    EXPECT_EQ(lost, 1);

    // Gone with UDisks2 too.
    int gone = 0;
    IdentityWatch other(model, {identityOf(model, sample::kData)}, [&] { ++gone; });
    model.reset({});
    EXPECT_EQ(gone, 1);
}
