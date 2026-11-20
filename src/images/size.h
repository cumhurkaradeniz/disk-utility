// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace images {

// The units a size is typed in, in decimal multiples as sizes are shown.
enum class SizeUnit {
    Megabytes,
    Gigabytes,
    Terabytes,
};

inline constexpr std::array<SizeUnit, 3> kSizeUnits = {SizeUnit::Megabytes, SizeUnit::Gigabytes, SizeUnit::Terabytes};

uint64_t unitBytes(SizeUnit unit);
// "MB", "GB", "TB".
std::string unitName(SizeUnit unit);

// Smaller images have no room for a partition table and a filesystem.
inline constexpr uint64_t kMinimumImageSize = 8ull << 20;

} // namespace images
