// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "udisks/backend.h"
#include "udisks/identity.h"
#include "udisks/operations.h"

#include "base/signal.h"
#include "base/weak_ptr.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace udisks {
class Model;
struct Object;
}

namespace dialogs {

// One call of an erase or a partition plan.
struct PlanStep
{
    enum class Kind {
        Unmount,
        Lock,
        Delete,
        // To size, before its partition shrinks.
        ShrinkFilesystem,
        ResizePartition,
        // To fill its partition, after it grew.
        GrowFilesystem,
        // A new partition map of type ("gpt", "dos") over the whole disk.
        CreateTable,
        // The block erased to the filesystem type.
        Format,
        // A partition of size at offset in the table at path, formatted to type.
        Create,
        SetLabel,
    };

    Kind kind = Kind::Format;
    // The block, or the partition table for Create.
    std::string path;
    uint64_t offset = 0;
    uint64_t size = 0;
    std::string type;
    // Create on a GUID partition map: the partition's own name.
    std::string partitionName;
    // Format, Create, CreateTable: the label, the erase and the encryption;
    // SetLabel: the label.
    udisks::FormatOptions options;
    // What the summary calls the volume or disk, such as "Data".
    std::string subject;
};

// A line of the summary and of the progress: "Delete “Data”",
// "Shrink “Data” to 200 GB"...
std::string describe(const PlanStep& step);
// "Deleting “Data”…", while the step runs.
std::string describeRunning(const PlanStep& step);

// UDisks2's job on the first of the objects that has one: the step's
// block, then its disk; null when none is running.
const udisks::Object* jobOf(const udisks::Model& model, const std::vector<std::string>& paths);

// Runs the steps one after another through the operations, stopping at the
// first that fails, and before any step once the block the plan was made
// for (a disk, or a volume) is not the one it was.
class PlanRunner
{
public:
    static constexpr const char* kChangedError = "org.cutefish.DiskUtility.Error.Changed";

    PlanRunner(const udisks::Model& model, udisks::Operations& operations, udisks::Identity subject,
               std::vector<PlanStep> steps);

    PlanRunner(const PlanRunner&) = delete;
    PlanRunner& operator=(const PlanRunner&) = delete;

    void start();

    const std::vector<PlanStep>& steps() const { return m_steps; }
    // The step running, or the one that failed; the step count once all ran.
    size_t current() const { return m_current; }
    bool isRunning() const { return m_running; }
    // The last result: the failed step's, or the last step's.
    const udisks::Result& result() const { return m_result; }
    bool succeeded() const { return !m_running && m_started && m_result.ok() && m_current == m_steps.size(); }

    // The index of the step that started.
    cutefish::base::Signal<size_t> stepStarted;
    // Every step ran, or one failed: result() says which.
    cutefish::base::Signal<> finished;

private:
    void runStep();

    const udisks::Model& m_model;
    udisks::Operations& m_operations;
    udisks::Identity m_subject;
    std::vector<PlanStep> m_steps;
    size_t m_current = 0;
    bool m_running = false;
    bool m_started = false;
    udisks::Result m_result;
    cutefish::base::WeakPtrFactory<PlanRunner> m_weakFactory{this};
};

} // namespace dialogs
