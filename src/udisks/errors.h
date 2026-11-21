// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>

// What UDisks2's errors mean, where only the message tells.
namespace udisks {

// Unmount, Lock or Eject failed because something still uses the volume.
bool isBusyError(const std::string& errorName, const std::string& errorMessage);
// Unlock was given the wrong passphrase.
bool isWrongPassphrase(const std::string& errorName, const std::string& errorMessage);
// Format wiped the block's signatures, but the kernel still found
// partitions on it afterwards.
bool isStaleTableError(const std::string& errorName, const std::string& errorMessage);

} // namespace udisks
