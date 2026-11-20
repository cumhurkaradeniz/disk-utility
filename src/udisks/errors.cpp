// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "udisks/errors.h"

namespace udisks {

static bool contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

bool isBusyError(const std::string& errorName, const std::string& errorMessage)
{
    return errorName == "org.freedesktop.UDisks2.Error.DeviceBusy" || contains(errorMessage, "target is busy")
            || contains(errorMessage, "Device or resource busy");
}

bool isWrongPassphrase(const std::string& errorName, const std::string& errorMessage)
{
    if (errorName != "org.freedesktop.UDisks2.Error.Failed")
        return false;
    // libblockdev's wording for a key that opens no slot, old and new.
    return contains(errorMessage, "Incorrect passphrase") || contains(errorMessage, "Operation not permitted")
            || contains(errorMessage, "No key available");
}

} // namespace udisks
