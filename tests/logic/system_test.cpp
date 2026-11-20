// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "fake_udisks.h"

#include "udisks/model.h"
#include "udisks/system.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <sys/sysmacros.h>
#include <unistd.h>

using namespace udisks;
using namespace udisks::fake;

static Variant mountPoints(const std::vector<std::string>& points)
{
    std::vector<std::vector<uint8_t>> bytes;
    for (const std::string& point : points)
        bytes.push_back(encodeBytes(point));
    return Variant(bytes);
}

TEST(SystemTest, SystemMountPoints)
{
    for (const char* point : {"/", "/boot", "/boot/efi", "/efi", "/usr", "/var", "/home"})
        EXPECT_TRUE(isSystemMountPoint(point)) << point;
    for (const char* point : {"/media/u/STICK", "/mnt", "/home/u", "/run/media/u/x", ""})
        EXPECT_FALSE(isSystemMountPoint(point)) << point;
}

TEST(SystemTest, TheRunningSystem)
{
    Model model;
    loadSample(model);
    EXPECT_TRUE(model.holdsSystem(sample::kRoot));
    EXPECT_TRUE(model.holdsSystem(sample::kEfi));
    // The disk holds what its partitions hold.
    EXPECT_TRUE(model.holdsSystem(sample::kNvme));
    EXPECT_TRUE(model.driveHoldsSystem(sample::kNvmeDrive));

    for (const std::string& block : {sample::kSata, sample::kData, sample::kStick, sample::kStickVolume,
                                     sample::kUsb, sample::kUnlocked, sample::kLocked, sample::kMbr,
                                     sample::kExtended, sample::kImage})
        EXPECT_FALSE(model.holdsSystem(block)) << block;
    for (const std::string& drive : {sample::kSataDrive, sample::kStickDrive, sample::kUsbDrive,
                                     sample::kCardDrive, sample::kMbrDrive})
        EXPECT_FALSE(model.driveHoldsSystem(drive)) << drive;
}

TEST(SystemTest, ByMountPointWithoutTheDevices)
{
    Model model;
    model.reset(sampleTree());
    model.setSystemDevices({{}, true});
    // UDisks2's mount points alone still mark the system.
    EXPECT_TRUE(model.holdsSystem(sample::kRoot));
    EXPECT_TRUE(model.holdsSystem(sample::kEfi));
    EXPECT_FALSE(model.holdsSystem(sample::kData));
}

TEST(SystemTest, UnknownDevicesProtectEverything)
{
    Model model;
    model.reset(sampleTree());
    // Before the probe, or when it found no root.
    for (const std::string& block : {sample::kData, sample::kStick, sample::kStickVolume, sample::kImage})
        EXPECT_TRUE(model.holdsSystem(block)) << block;
    EXPECT_TRUE(model.driveHoldsSystem(sample::kStickDrive));
    // What is not there holds nothing; the operations refuse it themselves.
    EXPECT_FALSE(model.holdsSystem("/org/freedesktop/UDisks2/block_devices/gone"));
}

TEST(SystemTest, BootPartitionsOfTheSystemDisk)
{
    Model model;
    loadSample(model);
    // An automounted EFI partition, not mounted now and unknown to the probe.
    model.setSystemDevices({{makedev(259, 2)}, true});
    model.changeProperties(sample::kEfi, interface::kFilesystem,
                           {{"MountPoints", Variant(std::vector<std::vector<uint8_t>>())}});
    EXPECT_TRUE(model.holdsSystem(sample::kEfi));

    // The same partition type on another disk is free.
    model.changeProperties(sample::kData, interface::kPartition,
                           {{"Type", Variant(std::string("C12A7328-F81F-11D2-BA4B-00A0C93EC93B"))}});
    EXPECT_FALSE(model.holdsSystem(sample::kData));
    EXPECT_TRUE(isBootPartitionType("21686148-6449-6e6f-744e-656564454649"));
    EXPECT_TRUE(isBootPartitionType("bc13c2ff-59e6-4262-a352-b275fd6f7172"));
    EXPECT_TRUE(isBootPartitionType("0xef"));
    EXPECT_FALSE(isBootPartitionType("0fc63daf-8483-4772-8e79-3d69d8477de4"));
}

TEST(SystemTest, ByDeviceNumberAlone)
{
    Model model;
    loadSample(model);
    // An LVM or RAID root: no mount point on the partition, only its number.
    model.setSystemDevices({{makedev(8, 1)}, true});
    EXPECT_TRUE(model.holdsSystem(sample::kData));
    EXPECT_TRUE(model.driveHoldsSystem(sample::kSataDrive));
    EXPECT_TRUE(model.holdsSystem(sample::kSata));
}

TEST(SystemTest, EncryptedHome)
{
    Model model;
    loadSample(model);
    model.changeProperties(sample::kCleartext, interface::kFilesystem, {{"MountPoints", mountPoints({"/home"})}});
    EXPECT_TRUE(model.holdsSystem(sample::kUnlocked));
    EXPECT_TRUE(model.holdsSystem(sample::kCleartext));
    EXPECT_TRUE(model.driveHoldsSystem(sample::kUsbDrive));
    EXPECT_FALSE(model.holdsSystem(sample::kLocked));
}

TEST(SystemTest, SwapInUseInsideAnExtendedPartition)
{
    Model model;
    loadSample(model);
    EXPECT_FALSE(model.holdsSystem(sample::kExtended));
    model.changeProperties(sample::kLogicalSwap, interface::kSwapspace, {{"Active", Variant(true)}});
    EXPECT_TRUE(model.holdsSystem(sample::kLogicalSwap));
    EXPECT_TRUE(model.holdsSystem(sample::kExtended));
    EXPECT_TRUE(model.holdsSystem(sample::kMbr));
    EXPECT_FALSE(model.holdsSystem(sample::kMbrLinux));
    EXPECT_TRUE(model.isInUse(sample::kExtended));
}

TEST(SystemTest, InUse)
{
    Model model;
    loadSample(model);
    EXPECT_TRUE(model.isInUse(sample::kStickVolume));
    EXPECT_TRUE(model.isInUse(sample::kStick));
    EXPECT_TRUE(model.isInUse(sample::kUnlocked));
    EXPECT_FALSE(model.isInUse(sample::kLocked));
    EXPECT_FALSE(model.isInUse(sample::kData));
    EXPECT_FALSE(model.isInUse(sample::kCard));
}

class ProbeTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path()
                / ("disk-utility-probe-" + std::to_string(::getpid()) + "-"
                   + testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
    }

    void TearDown() override { std::filesystem::remove_all(m_directory); }

    std::string write(const std::string& name, const std::string& text)
    {
        const std::filesystem::path path = m_directory / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << text;
        return path.string();
    }

    std::filesystem::path m_directory;
};

TEST_F(ProbeTest, MountTableAndSlaves)
{
    const std::string mountInfo = write(
        "mountinfo",
        "22 1 259:2 / / rw,noatime shared:1 - ext4 /dev/nvme0n1p2 rw\n"
        "23 22 259:1 / /boot/efi rw shared:2 - vfat /dev/nvme0n1p1 rw\n"
        "24 22 254:1 / /home rw shared:3 - ext4 /dev/mapper/home rw\n"
        "25 22 8:17 / /media/u/STICK rw - vfat /dev/sdb1 rw\n"
        "26 22 8:18 / /media/u/My\\040Disk rw - vfat /dev/sdb2 rw\n"
        "27 22 0:45 / /var rw - btrfs /nonexistent/device rw\n"
        "28 22 0:22 / /proc rw - proc proc rw\n");
    const std::string swaps = write("swaps", "Filename Type Size Used Priority\n");
    // /home is a device mapper device on sdc1.
    write("sys/dev/block/254:1/slaves/sdc1/dev", "8:33\n");

    const SystemDevices devices = probeSystemDevices(mountInfo, swaps, (m_directory / "sys").string());
    EXPECT_EQ(devices.devices, (std::set<uint64_t>{makedev(259, 2), makedev(259, 1), makedev(254, 1),
                                                   makedev(8, 33)}));
    EXPECT_TRUE(devices.known);
}

TEST_F(ProbeTest, MissingFiles)
{
    const SystemDevices devices = probeSystemDevices((m_directory / "none").string(),
                                                     (m_directory / "none").string(), (m_directory / "none").string());
    EXPECT_TRUE(devices.devices.empty());
    EXPECT_FALSE(devices.known);
}

TEST_F(ProbeTest, ARootOnNoBlockDevice)
{
    const std::string mountInfo = write("mountinfo", "22 1 0:30 / / rw - overlay overlay rw\n");
    const std::string swaps = write("swaps", "Filename Type Size Used Priority\n");
    const SystemDevices devices = probeSystemDevices(mountInfo, swaps, (m_directory / "sys").string());
    EXPECT_FALSE(devices.known);
}

TEST_F(ProbeTest, ALiveSystemsMedium)
{
    const std::string mountInfo = write(
        "mountinfo",
        "22 1 0:30 / / rw - overlay overlay rw\n"
        "23 22 8:17 / /run/live/medium ro - iso9660 /dev/sdb1 ro\n"
        "24 22 7:0 / /run/live/rootfs/filesystem.squashfs ro - squashfs /dev/loop0 ro\n");
    const std::string swaps = write("swaps", "Filename Type Size Used Priority\n");
    const SystemDevices devices = probeSystemDevices(mountInfo, swaps, (m_directory / "sys").string());
    EXPECT_TRUE(devices.known);
    EXPECT_EQ(devices.devices, (std::set<uint64_t>{makedev(8, 17), makedev(7, 0)}));
}

TEST_F(ProbeTest, APartitionOfARaid)
{
    // md126p2 on md126, made of sda and sdb, as Intel's firmware RAID is.
    const std::string mountInfo = write("mountinfo", "22 1 259:2 / / rw - ext4 /dev/md126p2 rw\n");
    const std::string swaps = write("swaps", "Filename Type Size Used Priority\n");
    write("sys/devices/virtual/block/md126/dev", "9:126\n");
    write("sys/devices/virtual/block/md126/slaves/sda/dev", "8:0\n");
    write("sys/devices/virtual/block/md126/slaves/sdb/dev", "8:16\n");
    write("sys/devices/virtual/block/md126/md126p2/dev", "259:2\n");
    write("sys/devices/virtual/block/md126/md126p2/partition", "2\n");
    std::filesystem::create_directories(m_directory / "sys/dev/block");
    std::filesystem::create_directory_symlink(m_directory / "sys/devices/virtual/block/md126/md126p2",
                                              m_directory / "sys/dev/block/259:2");
    std::filesystem::create_directory_symlink(m_directory / "sys/devices/virtual/block/md126",
                                              m_directory / "sys/dev/block/9:126");
    const SystemDevices devices = probeSystemDevices(mountInfo, swaps, (m_directory / "sys").string());
    EXPECT_EQ(devices.devices, (std::set<uint64_t>{makedev(259, 2), makedev(9, 126), makedev(8, 0),
                                                   makedev(8, 16)}));
}

TEST_F(ProbeTest, EveryDeviceOfABtrfsRoot)
{
    const std::string mountInfo = write("mountinfo", "22 1 0:45 /@ / rw - btrfs /nonexistent/sdz2 rw\n");
    const std::string swaps = write("swaps", "Filename Type Size Used Priority\n");
    write("sys/fs/btrfs/1234/devices/sdz2/dev", "8:130\n");
    write("sys/fs/btrfs/1234/devices/sdy1/dev", "8:113\n");
    write("sys/fs/btrfs/5678/devices/sdx1/dev", "8:97\n");
    const SystemDevices devices = probeSystemDevices(mountInfo, swaps, (m_directory / "sys").string());
    EXPECT_EQ(devices.devices, (std::set<uint64_t>{makedev(8, 130), makedev(8, 113)}));
    // The root's own number is anonymous, but its devices are known.
    EXPECT_TRUE(devices.known);
}
