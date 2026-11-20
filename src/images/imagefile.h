// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace udisks {
class Model;
}

namespace images {

// The file name patterns of the disk images that open: raw disk images and
// optical disc images.
const std::vector<std::string>& imagePatterns();
bool isDiskImageFile(const std::string& path);
// An optical disc image attaches read-only, as the disc it copies would.
bool attachesReadOnly(const std::string& path);

// A new image's file name from the name typed: ".img" added unless it ends
// in an image's extension already; empty for a name that is no file name.
std::string imageFileName(const std::string& name);

// The loop device the file is attached to, or empty. A hard link to the
// file, or its path through another mount, finds it too.
std::string loopOfFile(const udisks::Model& model, const std::string& path);

// The compression the file starts with ("xz", "gzip", "bzip2", "zstd",
// "ZIP", "7-Zip", "LZ4"), or empty for anything else.
std::string compressionOf(const std::string& path);

// A new file of the size holding no data yet, so it takes no room on the
// disk until written; refused when the disk has less room free than the
// size. Empty on success, otherwise why it failed; an existing file is left
// alone.
std::string createBlankFile(const std::string& path, uint64_t size);

} // namespace images
