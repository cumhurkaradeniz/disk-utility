// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "udisks/describe.h"
#include "udisks/tree.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace udisks {
class Model;
}

namespace views {

// One stretch of the usage bar and its legend entry.
struct UsageSegment
{
    enum class Kind {
        Used,
        Other,
        Free,
        // A disk's partition, coloured by its index.
        Partition,
    };

    Kind kind = Kind::Used;
    std::string title;
    uint64_t bytes = 0;
    size_t index = 0;

    bool operator==(const UsageSegment&) const = default;
};

// One cell of the information grid: its view is named "detail:" and key.
struct InfoItem
{
    std::string key;
    std::string title;
    std::string value;

    bool operator==(const InfoItem&) const = default;
};

// A UDisks2 job working on the selection.
struct JobLine
{
    std::string text;
    // From 0 to 1; none while UDisks2 cannot tell.
    std::optional<double> progress;

    bool operator==(const JobLine&) const = default;
};

// What the detail pane shows for a node, worked out from the model alone, so
// the pane is built again only when any of it changed.
struct DetailContent
{
    std::string icon;
    std::string name;
    // "512.11 GB · ext4 · NVMe".
    std::string subtitle;
    // Empty when nothing is known of the space: a volume not mounted.
    std::vector<UsageSegment> segments;
    // The grid's two columns, read down; one column when narrow.
    std::vector<InfoItem> left;
    std::vector<InfoItem> right;
    std::optional<JobLine> job;

    bool operator==(const DetailContent&) const = default;
};

DetailContent describeDetail(const udisks::Model& model, const udisks::Node& node,
                             const udisks::StatFunction& stat = udisks::statFilesystem);

// "Erasing", "Checking", for UDisks2's job operation.
std::string jobTitle(const std::string& operation);

} // namespace views
