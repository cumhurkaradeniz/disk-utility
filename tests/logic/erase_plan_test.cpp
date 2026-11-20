// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "fake_udisks.h"

#include "dialogs/erase_plan.h"
#include "dialogs/plan_runner.h"
#include "dialogs/volume_format.h"
#include "udisks/describe.h"
#include "udisks/identity.h"
#include "udisks/model.h"
#include "udisks/operations.h"

#include "base/event_loop.h"

#include <gtest/gtest.h>

using namespace dialogs;
using namespace udisks;
using namespace udisks::fake;
using Kind = dialogs::PlanStep::Kind;

class ErasePlanTest : public testing::Test
{
protected:
    void SetUp() override { loadSample(model); }

    static Node volume(const std::string& block, const std::string& disk)
    {
        Node node;
        node.kind = NodeKind::Volume;
        node.id = "v:" + block;
        node.block = block;
        node.disk = disk;
        return node;
    }

    static Node disk(const std::string& block, const std::string& drive)
    {
        Node node;
        node.kind = NodeKind::Disk;
        node.id = "d:" + block;
        node.block = block;
        node.disk = block;
        node.drive = drive;
        return node;
    }

    static std::vector<Kind> kinds(const std::vector<PlanStep>& steps)
    {
        std::vector<Kind> result;
        for (const PlanStep& step : steps)
            result.push_back(step.kind);
        return result;
    }

    // The methods called, without the questions about tools.
    std::vector<std::string> methods() const
    {
        std::vector<std::string> result;
        for (const Call& call : backend.calls) {
            if (!call.method.starts_with("Can"))
                result.push_back(call.path + " " + call.method);
        }
        return result;
    }

    VariantMap options(const Call& call, size_t index) { return call.arguments.at(index).value<VariantMap>(); }

    cutefish::base::EventLoop loop;
    Model model;
    FakeBackend backend;
    Operations operations{model, backend};
};

TEST_F(ErasePlanTest, AVolumeIsUnmountedThenFormatted)
{
    const EraseRequest request{.name = "photos", .filesystem = "vfat"};
    const std::vector<PlanStep> steps = eraseSteps(model, volume(sample::kStickVolume, sample::kStick), request);
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::Unmount, Kind::Format}));
    EXPECT_EQ(steps[0].path, sample::kStickVolume);
    EXPECT_EQ(steps[1].path, sample::kStickVolume);
    EXPECT_EQ(steps[1].type, "vfat");
    EXPECT_EQ(steps[1].options.label, "PHOTOS");
    EXPECT_EQ(steps[1].options.erase, "");
    EXPECT_FALSE(steps[1].options.takeOwnership);
    EXPECT_EQ(steps[1].subject, "STICK");
}

TEST_F(ErasePlanTest, EncryptedAndZeroed)
{
    const EraseRequest request{.name = "Vault",
                               .filesystem = "ext4",
                               .passphrase = "secret",
                               .encryptionType = "luks2",
                               .zeroFill = true};
    const std::vector<PlanStep> steps = eraseSteps(model, volume(sample::kUnlocked, sample::kUsb), request);
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::Unmount, Kind::Lock, Kind::Format}));
    EXPECT_EQ(steps[0].path, sample::kCleartext);
    EXPECT_EQ(steps[1].path, sample::kUnlocked);
    EXPECT_EQ(steps[2].path, sample::kUnlocked);
    EXPECT_EQ(steps[2].options.passphrase, "secret");
    EXPECT_EQ(steps[2].options.encryptionType, "luks2");
    EXPECT_EQ(steps[2].options.erase, "zero");
    EXPECT_TRUE(steps[2].options.takeOwnership);
    EXPECT_NE(describe(steps[2]).find("encrypted"), std::string::npos);
}

TEST_F(ErasePlanTest, AWholeDiskGetsANewMapAndOnePartition)
{
    EXPECT_TRUE(erasesWholeDisk(disk(sample::kUsb, sample::kUsbDrive)));
    EXPECT_FALSE(erasesWholeDisk(volume(sample::kData, sample::kSata)));
    const EraseRequest request{.name = "Big", .filesystem = "exfat", .scheme = "dos", .zeroFill = true};
    const std::vector<PlanStep> steps = eraseSteps(model, disk(sample::kUsb, sample::kUsbDrive), request);
    ASSERT_EQ(kinds(steps), (std::vector<Kind>{Kind::Unmount, Kind::Lock, Kind::CreateTable, Kind::Create}));
    EXPECT_EQ(steps[0].path, sample::kCleartext);
    EXPECT_EQ(steps[1].path, sample::kUnlocked);
    EXPECT_EQ(steps[2].path, sample::kUsb);
    EXPECT_EQ(steps[2].type, "dos");
    EXPECT_EQ(steps[2].options.erase, "zero");
    const uint64_t size = model.object(sample::kUsb)->block->size;
    EXPECT_EQ(steps[3].path, sample::kUsb);
    EXPECT_EQ(steps[3].offset, kMiB);
    EXPECT_EQ(steps[3].size, alignDown(size - kMiB) - kMiB);
    EXPECT_EQ(steps[3].type, "exfat");
    EXPECT_EQ(steps[3].options.label, "Big");
    // Zeroed once, by the map's step; no names on MBR.
    EXPECT_EQ(steps[3].options.erase, "");
    EXPECT_EQ(steps[3].partitionName, "");

    const std::vector<PlanStep> gpt = eraseSteps(model, disk(sample::kCard, sample::kCardDrive),
                                                 {.name = "Card", .filesystem = "ext4"});
    ASSERT_EQ(kinds(gpt), (std::vector<Kind>{Kind::CreateTable, Kind::Create}));
    EXPECT_EQ(gpt[0].type, "gpt");
    EXPECT_EQ(gpt[1].partitionName, "Card");
}

TEST_F(ErasePlanTest, RunsTheStepsInOrder)
{
    backend.responder = [](const Call&) -> std::optional<Result> { return Result{}; };
    const std::vector<PlanStep> steps = eraseSteps(model, disk(sample::kUsb, sample::kUsbDrive),
                                                   {.name = "Big", .filesystem = "ext4", .scheme = "gpt"});
    PlanRunner runner(model, operations, identityOf(model, sample::kUsb), steps);
    std::vector<size_t> started;
    bool finished = false;
    auto a = runner.stepStarted.connect([&](size_t index) { started.push_back(index); });
    auto b = runner.finished.connect([&] { finished = true; });
    runner.start();
    EXPECT_TRUE(runner.isRunning());
    for (int i = 0; i < 8 && !finished; ++i)
        runPosted();
    ASSERT_TRUE(finished);
    EXPECT_TRUE(runner.succeeded());
    EXPECT_EQ(started, (std::vector<size_t>{0, 1, 2, 3}));
    EXPECT_EQ(methods(), (std::vector<std::string>{sample::kCleartext + " Unmount", sample::kUnlocked + " Lock",
                                                   sample::kUsb + " Format", sample::kUsb + " CreatePartitionAndFormat"}));
    const Call& format = backend.calls[2];
    EXPECT_EQ(format.arguments.at(0).value<std::string>(), "gpt");
    const Call& create = backend.calls[3];
    EXPECT_EQ(create.arguments.at(0).value<uint64_t>(), kMiB);
    EXPECT_EQ(create.arguments.at(3).value<std::string>(), "Big");
    EXPECT_EQ(create.arguments.at(5).value<std::string>(), "ext4");
    EXPECT_EQ(options(create, 6).at("label").value<std::string>(), "Big");
}

TEST_F(ErasePlanTest, StopsWhenAnotherDiskTakesThePath)
{
    // The stick is pulled out after the first step, and another goes in as sdb.
    backend.responder = [this](const Call& call) -> std::optional<Result> {
        if (call.method == "Unmount")
            model.changeProperties(sample::kStickDrive, interface::kDrive,
                                   {{"Serial", Variant(std::string("ANOTHER"))}, {"Id", Variant(std::string("Other"))}});
        return Result{};
    };
    PlanRunner runner(model, operations, identityOf(model, sample::kStick),
                      eraseSteps(model, disk(sample::kStick, sample::kStickDrive),
                                 {.name = "x", .filesystem = "vfat", .scheme = "dos"}));
    bool finished = false;
    auto subscription = runner.finished.connect([&] { finished = true; });
    runner.start();
    for (int i = 0; i < 8 && !finished; ++i)
        runPosted();
    ASSERT_TRUE(finished);
    EXPECT_FALSE(runner.succeeded());
    EXPECT_EQ(runner.result().errorName, PlanRunner::kChangedError);
    // Only the unmount went out; nothing was formatted.
    EXPECT_EQ(methods(), std::vector<std::string>{sample::kStickVolume + " Unmount"});
}

TEST_F(ErasePlanTest, StopsAtTheFirstFailure)
{
    backend.responder = [](const Call& call) -> std::optional<Result> {
        if (call.method == "Lock")
            return error("org.freedesktop.UDisks2.Error.Failed", "busy");
        return Result{};
    };
    PlanRunner runner(model, operations, identityOf(model, sample::kUnlocked),
                      eraseSteps(model, volume(sample::kUnlocked, sample::kUsb), {.name = "x", .filesystem = "ext4"}));
    bool finished = false;
    auto subscription = runner.finished.connect([&] { finished = true; });
    runner.start();
    for (int i = 0; i < 8 && !finished; ++i)
        runPosted();
    ASSERT_TRUE(finished);
    EXPECT_FALSE(runner.succeeded());
    EXPECT_EQ(runner.current(), 1u);
    EXPECT_EQ(runner.result().errorMessage, "busy");
    EXPECT_EQ(methods(), (std::vector<std::string>{sample::kCleartext + " Unmount", sample::kUnlocked + " Lock"}));
}

TEST_F(ErasePlanTest, TheSystemIsNeverFormatted)
{
    backend.responder = [](const Call&) -> std::optional<Result> { return Result{}; };
    PlanRunner runner(model, operations, identityOf(model, sample::kRoot),
                      eraseSteps(model, volume(sample::kRoot, sample::kNvme), {.name = "x", .filesystem = "ext4"}));
    bool finished = false;
    auto subscription = runner.finished.connect([&] { finished = true; });
    runner.start();
    for (int i = 0; i < 8 && !finished; ++i)
        runPosted();
    ASSERT_TRUE(finished);
    EXPECT_FALSE(runner.succeeded());
    EXPECT_EQ(runner.result().errorName, Operations::kSystemError);
    // Unmounting / is refused before anything is sent.
    EXPECT_TRUE(methods().empty());

    PlanRunner whole(model, operations, identityOf(model, sample::kNvme),
                     eraseSteps(model, disk(sample::kNvme, sample::kNvmeDrive), {.name = "x", .filesystem = "ext4"}));
    finished = false;
    auto again = whole.finished.connect([&] { finished = true; });
    whole.start();
    for (int i = 0; i < 8 && !finished; ++i)
        runPosted();
    ASSERT_TRUE(finished);
    EXPECT_EQ(whole.result().errorName, Operations::kSystemError);
    EXPECT_TRUE(methods().empty());
}

TEST_F(ErasePlanTest, DescribesEachStep)
{
    PlanStep step{.kind = Kind::ResizePartition, .path = sample::kData, .size = 200000000000, .subject = "Data"};
    EXPECT_EQ(describe(step), "Resize “Data” to " + formatSize(200000000000));
    step.kind = Kind::Delete;
    EXPECT_EQ(describe(step), "Delete “Data”");
    EXPECT_EQ(describeRunning(step), "Deleting “Data”…");
    step = {.kind = Kind::CreateTable, .type = "gpt", .subject = "Disk"};
    EXPECT_EQ(describe(step), "Create a GUID Partition Map on “Disk”");
}
