// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "dialogs/plan_runner.h"
#include "udisks/tree.h"

#include <string>
#include <vector>

namespace udisks {
class Model;
}

namespace dialogs {

// What the Erase dialog was given.
struct EraseRequest
{
    std::string name;
    // UDisks2's filesystem type, one of erasableFilesystems().
    std::string filesystem;
    // For a whole disk: "gpt" or "dos".
    std::string scheme = "gpt";
    // Encrypts with LUKS when set.
    std::string passphrase;
    // "luks1", "luks2"; empty for UDisks2's default.
    std::string encryptionType;
    // Overwrites the data with zeros first.
    bool zeroFill = false;
};

// A whole disk gets a new partition map and one partition over it; a
// volume or partition is formatted in place. Either way what is mounted or
// unlocked on it is unmounted and locked first.
std::vector<PlanStep> eraseSteps(const udisks::Model& model, const udisks::Node& node, const EraseRequest& request);

// The selection is erased as a whole disk, with a partition scheme to
// choose.
bool erasesWholeDisk(const udisks::Node& node);

} // namespace dialogs
