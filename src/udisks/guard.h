// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>

namespace udisks {

class Model;

// What a call changes, for what may be done on a disk that holds the
// running system.
enum class Change {
    // One volume only: unmounting, renaming, checking and repairing,
    // locking, formatting a partition in place.
    Volume,
    // The partition table or the whole disk: creating, deleting, resizing
    // or retyping a partition, formatting the whole disk or giving it a new
    // table, restoring onto it.
    Table,
    // The drive itself: ejecting, powering off, detaching an image.
    Drive,
};

// Why the change may not be made to the block; empty when it may. A volume
// change is refused for a block that holds the running system itself; a
// table or drive change also for any block on a disk or drive that holds
// it. Everything is refused while the system's devices are not known, and
// for a block that is gone. The actions and the operations both ask here.
std::string systemRefusal(const Model& model, const std::string& block, Change change);
// A Format of the block: of a partition in place, or of the whole disk.
Change formatChange(const Model& model, const std::string& block);

} // namespace udisks
