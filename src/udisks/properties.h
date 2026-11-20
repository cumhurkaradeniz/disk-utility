// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "udisks/tree.h"
#include "udisks/types.h"

#include <cstdint>
#include <string>
#include <vector>

// Every property of a node, in words, for the Info window.
namespace udisks {

class Model;

struct Property
{
    std::string name;
    std::string value;
    // As UDisks2 names it: "IdLabel".
    std::string key;

    bool operator==(const Property&) const = default;
};

struct PropertyGroup
{
    std::string title;
    std::string interface;
    // By title.
    std::vector<Property> properties;

    bool operator==(const PropertyGroup&) const = default;
};

// A disk: its drive (Drive, Drive.Ata, NVMe.Controller) and its block
// (Block, Partition Table, Loop, Filesystem). A volume or partition: its
// block (Block, Partition, Encrypted, Filesystem, Swapspace), then an
// unlocked volume's cleartext block.
std::vector<PropertyGroup> describeProperties(const Model& model, const Node& node);

// "Label" for IdLabel, "Read Only" for ReadOnly; unknown names split at
// their capitals.
std::string propertyTitle(const std::string& name);
// "Block Device", "File System", "Encryption"...; the interface's last part
// for one without a title.
std::string interfaceTitle(const std::string& interface);
// The property's value in words: sizes in bytes and units, devices for
// byte strings and object paths, Yes or No, dates, partition types and
// flags; an em dash for nothing.
std::string formatProperty(const Model& model, const std::string& interface, const std::string& name,
                           const Variant& value, const Object& object);

// Digits grouped as the system's locale groups them.
std::string formatCount(uint64_t count);
// "512.11 GB (512,110,190,592 bytes)".
std::string formatBytes(uint64_t bytes);
// The local date and time; empty for 0.
std::string formatDate(int64_t secondsSinceEpoch);
// "EFI System", "Linux Filesystem", "Microsoft Basic Data", "Linux Swap"...
// for a GPT GUID or an MBR type byte ("0x83"); empty when unknown.
std::string partitionTypeName(const std::string& tableType, const std::string& type);
// "Bootable", "Required, Hidden"...; "None" without flags.
std::string partitionFlagsText(const std::string& tableType, uint64_t flags);

} // namespace udisks
