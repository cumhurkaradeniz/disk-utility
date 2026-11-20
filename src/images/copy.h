// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace images {

struct CopyResult
{
    enum class Status {
        Done,
        Cancelled,
        ReadFailed,
        WriteFailed,
        // The source had fewer bytes than were to be copied.
        SourceEnded,
        // The data did not reach the target's storage.
        SyncFailed,
    };

    Status status = Status::Done;
    // The errno of a failed read, write or sync.
    int error = 0;
    uint64_t copied = 0;

    bool ok() const { return status == Status::Done; }
    // What went wrong, for an alert; empty when nothing did.
    std::string message() const;
};

struct CopyOptions
{
    // A run of zeros in the source becomes a hole in the target: for a new
    // or emptied regular file, which is then cut to the length. Ignored for
    // other targets.
    bool sparse = false;
    size_t bufferSize = 4u << 20;
};

// Copies the first length bytes of source to the start of target, both by
// offset, retrying interrupted and short reads and writes, then syncs the
// target. Between buffers it stops when cancel is set, and reports the
// bytes copied so far to progress. Blocks; for a worker thread.
CopyResult copyData(int source, int target, uint64_t length, const CopyOptions& options,
                    const std::atomic<bool>& cancel, const std::function<void(uint64_t copied)>& progress = {});

// copyData() on a thread of its own, its progress and result handed to the
// loop that started it. Destroying the job cancels the copy and waits for
// the thread; nothing is handed over after that.
class CopyJob
{
public:
    CopyJob();
    ~CopyJob();

    CopyJob(const CopyJob&) = delete;
    CopyJob& operator=(const CopyJob&) = delete;

    // Owns both descriptors from here and closes them when the copy is over.
    // One copy per job.
    void start(int source, int target, uint64_t length, CopyOptions options = {});
    // The copy stops at its next buffer and finishes as cancelled.
    void cancel();
    bool isRunning() const { return m_running; }

    // At most ten times a second while copying.
    std::function<void(uint64_t copied, uint64_t total)> onProgress;
    // Once, after the descriptors are closed.
    std::function<void(const CopyResult& result)> onFinished;

private:
    std::thread m_thread;
    std::atomic<bool> m_cancel = false;
    bool m_running = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace images
