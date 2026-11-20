// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "udisks/types.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// A drive's S.M.A.R.T. data in words: its overall status for the detail
// pane, and the attributes and health log for the S.M.A.R.T. dialog.
namespace udisks {

class Model;

enum class SmartStatus {
    // No Drive.Ata or NVMe.Controller, S.M.A.R.T. off, or not read yet.
    NotSupported,
    Verified,
    // The drive predicts its failure, an attribute is past its threshold,
    // or the NVMe health log carries a critical warning.
    Failing,
};

SmartStatus smartStatus(const Model& model, const std::string& drive);
// "Verified", "Failing", "Not Supported".
std::string smartStatusName(SmartStatus status);

// The drive runs self-tests. UDisks2 has no property for it: it reports
// a self-test state only for an NVMe controller that supports them, and for
// an ATA drive whose S.M.A.R.T. data it has read.
bool canSelftest(const Object& drive);

// "37 °C" from kelvin; empty for 0 (unknown).
std::string formatTemperature(double kelvin);
// "1,108 hours" or, past two days, "46 days (1,108 hours)".
std::string formatPowerOnHours(uint64_t hours);
// "Completed without error", "In progress, 40% left", "Aborted"...; empty
// when no test has run.
std::string selftestStatusText(const std::string& status, int32_t percentRemaining);

// One row of Drive.Ata's SmartGetAttributes: a(ysqiiixia{sv}).
struct AtaAttribute
{
    uint8_t id = 0;
    std::string name;
    uint16_t flags = 0;
    // Normalised values; -1 when the drive gives none.
    int32_t value = -1;
    int32_t worst = -1;
    int32_t threshold = -1;
    int64_t pretty = 0;
    // 0 unknown, 1 a count, 2 milliseconds, 3 sectors, 4 millikelvin.
    int32_t prettyUnit = 0;

    // Bit 0: the drive fails when this attribute does.
    bool preFail() const { return flags & 1; }
    // At or below its threshold now.
    bool failing() const { return value > 0 && threshold > 0 && value <= threshold; }
};

// Empty when the result holds no attributes.
std::vector<AtaAttribute> parseAtaAttributes(const Variant::Items& values);
// "Power-On Hours" from "power-on-hours"; "Attribute 231" without a name.
std::string attributeTitle(const AtaAttribute& attribute);
// The pretty value in its unit: "4,000 hours", "37 °C", "12 sectors".
std::string attributeValue(const AtaAttribute& attribute);

// NVMe.Controller's SmartGetAttributes a{sv} in rows: spare, wear, data
// read and written, power cycles, unsafe shutdowns, media errors.
std::vector<std::pair<std::string, std::string>> describeNvmeHealth(const Variant::Items& values);

} // namespace udisks
