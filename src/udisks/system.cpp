// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "udisks/system.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace udisks {

bool isSystemMountPoint(const std::string& mountPoint)
{
    for (const char* system : {"/", "/boot", "/boot/efi", "/efi", "/usr", "/var", "/home"}) {
        if (mountPoint == system)
            return true;
    }
    return false;
}

// Mount table fields escape spaces and the like as octal.
static std::string unescape(const std::string& text)
{
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 3 < text.size()) {
            const std::string digits = text.substr(i + 1, 3);
            if (digits.find_first_not_of("01234567") == std::string::npos) {
                result.push_back(char(std::stoi(digits, nullptr, 8)));
                i += 3;
                continue;
            }
        }
        result.push_back(text[i]);
    }
    return result;
}

static bool parseDeviceNumber(const std::string& text, uint64_t& device)
{
    unsigned major = 0;
    unsigned minor = 0;
    char colon = 0;
    std::istringstream stream(text);
    if (!(stream >> major >> colon >> minor) || colon != ':')
        return false;
    device = makedev(major, minor);
    return true;
}

static std::string numberText(uint64_t device)
{
    return std::to_string(major(device)) + ":" + std::to_string(minor(device));
}

static uint64_t readDeviceNumber(const std::filesystem::path& file)
{
    std::ifstream stream(file);
    std::string text;
    uint64_t device = 0;
    if (std::getline(stream, text) && parseDeviceNumber(text, device))
        return device;
    return 0;
}

static void addWithSlaves(SystemDevices& system, const std::string& sysfs, uint64_t device, int depth = 0)
{
    if (device == 0 || depth > 16 || !system.devices.insert(device).second)
        return;
    const std::filesystem::path node = std::filesystem::path(sysfs) / "dev" / "block" / numberText(device);
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(node / "slaves", error))
        addWithSlaves(system, sysfs, readDeviceNumber(entry.path() / "dev"), depth + 1);
    // A partition has no slaves of its own: those of the disk it is on, such
    // as the members of a partitioned RAID, are under it too.
    if (std::filesystem::exists(node / "partition", error)) {
        const std::filesystem::path disk = std::filesystem::canonical(node, error).parent_path();
        if (!error)
            addWithSlaves(system, sysfs, readDeviceNumber(disk / "dev"), depth + 1);
    }
}

// Every device of the multi-device Btrfs filesystem one of whose devices has
// the name, as /sys/fs/btrfs/<fsid>/devices lists them; false for none.
static bool addBtrfsDevices(SystemDevices& system, const std::string& sysfs, const std::string& name)
{
    bool found = false;
    std::error_code error;
    for (const auto& filesystem : std::filesystem::directory_iterator(std::filesystem::path(sysfs) / "fs" / "btrfs",
                                                                       error)) {
        const std::filesystem::path devices = filesystem.path() / "devices";
        std::error_code missing;
        if (!std::filesystem::exists(devices / name, missing))
            continue;
        for (const auto& device : std::filesystem::directory_iterator(devices, missing)) {
            const uint64_t number = readDeviceNumber(device.path() / "dev");
            found = found || number != 0;
            addWithSlaves(system, sysfs, number);
        }
    }
    return found;
}

// A device node's number, for mount sources and swaps.
static uint64_t nodeNumber(const std::string& path)
{
    struct stat info = {};
    if (path.empty() || path[0] != '/' || ::stat(path.c_str(), &info) != 0 || !S_ISBLK(info.st_mode))
        return 0;
    return info.st_rdev;
}

bool isBootPartitionType(const std::string& type)
{
    std::string lower;
    for (char c : type)
        lower.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    return lower == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" || lower == "21686148-6449-6e6f-744e-656564454649"
            || lower == "bc13c2ff-59e6-4262-a352-b275fd6f7172" || lower == "0xef" || lower == "0xea";
}

SystemDevices probeSystemDevices(const std::string& mountInfo, const std::string& swaps, const std::string& sysfs)
{
    SystemDevices system;
    bool rootFound = false;
    bool liveMedium = false;

    // "36 35 98:0 /mnt1 /mnt2 rw,noatime master:1 - ext3 /dev/root rw": the
    // device number is the third field, the source the second after "-".
    std::ifstream mounts(mountInfo);
    if (!mounts)
        return system;
    for (std::string line; std::getline(mounts, line);) {
        std::istringstream fields(line);
        std::string id, parent, number, root, point;
        if (!(fields >> id >> parent >> number >> root >> point))
            continue;
        point = unescape(point);
        const bool live = point == "/run/live" || point.starts_with("/run/live/");
        if (!isSystemMountPoint(point) && !live)
            continue;
        std::string field;
        while (fields >> field && field != "-") {
        }
        std::string type, source;
        fields >> type >> source;
        source = unescape(source);
        uint64_t device = 0;
        // Btrfs and others give an anonymous device number of major 0; the
        // source names the block device then.
        if (!parseDeviceNumber(number, device) || major(device) == 0)
            device = nodeNumber(source);
        addWithSlaves(system, sysfs, device);
        bool found = device != 0;
        if (type == "btrfs" && source.starts_with("/")) {
            std::error_code error;
            const std::filesystem::path resolved = std::filesystem::canonical(source, error);
            const std::string name = (error ? std::filesystem::path(source) : resolved).filename().string();
            found = addBtrfsDevices(system, sysfs, name) || found;
        }
        if (point == "/" && found)
            rootFound = true;
        if (live && device != 0)
            liveMedium = true;
    }
    // A live system's root is an overlay in memory over a medium mounted
    // under /run/live.
    system.known = rootFound || liveMedium;

    std::ifstream swapList(swaps);
    std::string header;
    std::getline(swapList, header);
    for (std::string line; std::getline(swapList, line);) {
        std::istringstream fields(line);
        std::string path, type;
        if (fields >> path >> type && type == "partition")
            addWithSlaves(system, sysfs, nodeNumber(unescape(path)));
    }

    return system;
}

} // namespace udisks
