// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>
#include <vector>

namespace udisks {

// The names of the processes keeping a filesystem busy: an open file, a
// working folder or a mapped file under one of the mount points, as far as
// the process table lets this user see. Sorted, each name once.
std::vector<std::string> processesUsing(const std::vector<std::string>& mountPoints,
                                        const std::string& procRoot = "/proc");

} // namespace udisks
