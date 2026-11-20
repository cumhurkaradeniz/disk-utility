// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "images/formats.h"

namespace images {

std::string tableType(Scheme scheme)
{
    switch (scheme) {
    case Scheme::Gpt:
        return "gpt";
    case Scheme::Mbr:
        return "dos";
    case Scheme::None:
        return {};
    }
    return {};
}

} // namespace images
