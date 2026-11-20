// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "udisks/holders.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace udisks {

static bool under(const std::string& path, const std::vector<std::string>& mountPoints)
{
    for (const std::string& mountPoint : mountPoints) {
        if (mountPoint.empty() || mountPoint == "/")
            continue;
        if (path == mountPoint || path.starts_with(mountPoint + "/"))
            return true;
    }
    return false;
}

static bool linksUnder(const fs::path& link, const std::vector<std::string>& mountPoints)
{
    std::error_code error;
    const fs::path target = fs::read_symlink(link, error);
    return !error && under(target.string(), mountPoints);
}

static bool holds(const fs::path& process, const std::vector<std::string>& mountPoints)
{
    if (linksUnder(process / "cwd", mountPoints) || linksUnder(process / "root", mountPoints)
        || linksUnder(process / "exe", mountPoints))
        return true;
    std::error_code error;
    for (fs::directory_iterator it(process / "fd", error), end; !error && it != end; it.increment(error)) {
        if (linksUnder(it->path(), mountPoints))
            return true;
    }
    std::ifstream maps(process / "maps");
    std::string line;
    while (std::getline(maps, line)) {
        const size_t slash = line.find('/');
        if (slash != std::string::npos && under(line.substr(slash), mountPoints))
            return true;
    }
    return false;
}

std::vector<std::string> processesUsing(const std::vector<std::string>& mountPoints, const std::string& procRoot)
{
    std::vector<std::string> names;
    std::error_code error;
    for (fs::directory_iterator it(procRoot, error), end; !error && it != end; it.increment(error)) {
        const std::string pid = it->path().filename().string();
        if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](char c) { return c >= '0' && c <= '9'; }))
            continue;
        if (!holds(it->path(), mountPoints))
            continue;
        std::ifstream comm(it->path() / "comm");
        std::string name;
        std::getline(comm, name);
        if (!name.empty())
            names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

} // namespace udisks
