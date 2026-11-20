// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>

namespace images {

// A new image's partition map.
enum class Scheme {
    Gpt,
    Mbr,
    // A filesystem on the whole image, without a partition map.
    None,
};

// Block.Format's type for the map: "gpt", "dos"; empty for none.
std::string tableType(Scheme scheme);

} // namespace images
