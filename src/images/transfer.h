// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "images/copy.h"

#include "udisks/identity.h"
#include "udisks/operations.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace udisks {
class Model;
}

namespace images {

// One end of a copy: a block device, opened through UDisks2, or a file.
struct Endpoint
{
    std::string block;
    std::string file;
    // The block as it was when it was chosen; the copy stops before it
    // touches another one at its path. Unset, the copy takes it as it starts.
    udisks::Identity identity;

    static Endpoint ofBlock(std::string block) { return {std::move(block), {}, {}}; }
    // With the block's identity as it is now.
    static Endpoint ofBlock(const udisks::Model& model, std::string block);
    static Endpoint ofFile(std::string file) { return {{}, std::move(file), {}}; }
    bool isBlock() const { return !block.empty(); }
};

// The filesystems mounted on the block or anything on it: its partitions
// for a disk, the cleartext block of an unlocked encrypted one.
std::vector<std::string> mountedOn(const udisks::Model& model, const std::string& block);
// The unlocked encrypted blocks on it, the same way.
std::vector<std::string> unlockedOn(const udisks::Model& model, const std::string& block);
// Bytes in the block or the file; none when it cannot be told.
std::optional<uint64_t> sizeOf(const udisks::Model& model, const Endpoint& endpoint);

// One block is the other or lies inside it: a partition on a disk, a
// cleartext block on its encrypted one.
bool overlaps(const udisks::Model& model, const std::string& block, const std::string& other);

// The file's folder is on a filesystem mounted on the block, or on
// anything on it.
bool isOnFilesystemOf(const udisks::Model& model, const std::string& block, const std::string& path);

// Why the source cannot be restored onto the target block, or empty when it
// can: the same device, one inside the other, an image attached and mounted
// or attached as the target, or a target too small.
std::string restoreProblem(const udisks::Model& model, const Endpoint& source, const std::string& target);

// Copies a disk, volume or image file onto a file (an image of it) or a
// block (restoring onto it). What is mounted on either block is unmounted
// first and their unlocked encrypted volumes are locked, as both are opened
// exclusively. A file
// made as the target is removed again unless the copy completes.
class Transfer
{
public:
    enum class Step {
        Unmounting,
        Opening,
        Copying,
    };

    struct Outcome
    {
        bool ok = false;
        bool cancelled = false;
        std::string error;
        uint64_t copied = 0;
    };

    Transfer(udisks::Model& model, udisks::Operations& operations);
    ~Transfer();

    Transfer(const Transfer&) = delete;
    Transfer& operator=(const Transfer&) = delete;

    // One copy per transfer.
    void start(Endpoint source, Endpoint target);
    // Stops at the next buffer, or once the call out returns.
    void cancel();
    bool isRunning() const { return m_running; }
    uint64_t total() const { return m_length; }

    std::function<void(Step step)> onStep;
    std::function<void(uint64_t copied, uint64_t total)> onProgress;
    std::function<void(const Outcome& outcome)> onFinished;

private:
    void release(size_t index);
    void openSource();
    void openTarget();
    void copy();
    void fail(const udisks::Result& result);
    void fail(std::string error);
    void finish(Outcome outcome);
    void closeFiles();
    // Both blocks are still the ones that were chosen; fails the copy when not.
    bool stillTheSame();
    template<typename F>
    auto guard(F f);

    udisks::Model& m_model;
    udisks::Operations& m_operations;
    Endpoint m_source;
    Endpoint m_target;
    uint64_t m_length = 0;
    // Unmounts, then locks, one after another.
    std::vector<std::pair<bool, std::string>> m_releases;
    int m_sourceFd = -1;
    int m_targetFd = -1;
    bool m_createdTarget = false;
    bool m_running = false;
    bool m_cancelled = false;
    std::unique_ptr<CopyJob> m_job;
    // Both blocks stay busy while the data goes past UDisks2.
    std::vector<udisks::Operations::Hold> m_holds;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace images
