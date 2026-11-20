// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "images/size.h"

namespace images {

uint64_t unitBytes(SizeUnit unit)
{
    switch (unit) {
    case SizeUnit::Megabytes:
        return 1000ull * 1000;
    case SizeUnit::Gigabytes:
        return 1000ull * 1000 * 1000;
    case SizeUnit::Terabytes:
        return 1000ull * 1000 * 1000 * 1000;
    }
    return 1;
}

std::string unitName(SizeUnit unit)
{
    switch (unit) {
    case SizeUnit::Megabytes:
        return "MB";
    case SizeUnit::Gigabytes:
        return "GB";
    case SizeUnit::Terabytes:
        return "TB";
    }
    return {};
}

} // namespace images
