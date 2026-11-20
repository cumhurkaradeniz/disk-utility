// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <set>
#include <string>

namespace udisks {

// The block devices the running system lives on, by device number
// (makedev(major, minor), as Block.DeviceNumber): the ones mounted at a
// system mount point or under /run/live (a live system's medium), the swap
// devices in use, every member of a multi-device Btrfs root, and every
// device under them (a partition under its device mapper or RAID device,
// the members of the RAID a partition lies on), so an encrypted, LVM or
// RAID root still marks its partitions.
struct SystemDevices
{
    std::set<uint64_t> devices;
    // The mount table was read and the root's device found, or a live
    // system's medium. While it is not, every block counts as holding the
    // system.
    bool known = false;

    bool contains(uint64_t device) const { return devices.contains(device); }
    bool operator==(const SystemDevices&) const = default;
};

// "/", "/boot", "/boot/efi", "/efi", "/usr", "/var" and "/home": a volume
// mounted at one of them is never unmounted, erased or repaired here.
bool isSystemMountPoint(const std::string& mountPoint);

// Reads the mount table, the swaps in use and sysfs. The paths are for the
// tests.
SystemDevices probeSystemDevices(const std::string& mountInfo = "/proc/self/mountinfo",
                                 const std::string& swaps = "/proc/swaps", const std::string& sysfs = "/sys");

// A GPT or MBR partition type the firmware or boot loader reads without it
// being mounted: an EFI system partition, a BIOS boot partition, an
// extended boot loader partition.
bool isBootPartitionType(const std::string& type);

} // namespace udisks
