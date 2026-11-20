// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "scenario.h"

#include "dialogs/choice_button.h"
#include "dialogs/partition.h"
#include "dialogs/pie_chart.h"
#include "dialogs/volume_format.h"
#include "udisks/describe.h"
#include "udisks/model.h"
#include "views/context.h"

#include "ui/keys.h"
#include "ui/text_field.h"

#include <gtest/gtest.h>

#include <tuple>

using namespace cutefish::ui;
using namespace udisks::fake;
using cutefish::base::dbus::Variant;
using cutefish::base::dbus::VariantMap;

static constexpr uint64_t kGB = 1000 * 1000 * 1000;

// The partition editor over the sample disks; every filesystem's tools are
// there and NTFS resizes offline.
class PartitionTest : public testing::Test
{
protected:
    void SetUp() override
    {
        scenario.backend().responder = [this](const udisks::Call& call) -> std::optional<udisks::Result> {
            if (call.method == "CanFormat")
                return udisks::Result{{}, {}, {Variant(std::tuple<bool, std::string>(true, ""))}};
            if (call.method == "CanResize")
                return udisks::Result{{}, {}, {Variant(std::tuple<bool, uint64_t, std::string>(true, 2 | 4, ""))}};
            if (call.method == failing)
                return error("org.freedesktop.UDisks2.Error.Failed", "Resize failed");
            return udisks::Result{};
        };
    }

    std::vector<std::string> calls() const
    {
        std::vector<std::string> result;
        for (const udisks::Call& call : const_cast<PartitionTest*>(this)->scenario.backend().calls) {
            if (!call.method.starts_with("Can"))
                result.push_back(call.path + " " + call.method);
        }
        return result;
    }

    std::unique_ptr<TestDriver> open(const std::string& id)
    {
        scenario.window->select(id);
        scenario.driver.layout();
        scenario.driver.click("toolbar:partition");
        scenario.driver.layout();
        Window* window = scenario.driver.dialog();
        if (!window) {
            ADD_FAILURE() << "no dialog open";
            return nullptr;
        }
        auto driver = std::make_unique<TestDriver>(*window, [](const std::string& message) { ADD_FAILURE() << message; });
        driver->setKeyboardFocus(true);
        runPosted();
        driver->layout();
        return driver;
    }

    static void replaceText(TestDriver& driver, const std::string& name, const std::string& text)
    {
        driver.click(name);
        driver.typeKey(key::letter('a'), ControlModifier);
        driver.typeKey(key::BackSpace);
        driver.typeText(text);
    }

    static bool enabled(TestDriver& driver, const std::string& name)
    {
        const View* view = driver.find(name);
        return view && view->isEnabled();
    }

    bool runs(TestDriver& driver)
    {
        return TestDriver::waitFor([&] {
            driver.layout();
            return driver.shows("partition:done");
        });
    }

    Scenario scenario;
    std::string failing;
};

TEST_F(PartitionTest, AddsAPartition)
{
    auto driver = open("v:" + sample::kData);
    ASSERT_TRUE(driver);
    EXPECT_TRUE(driver->shows("partition:pie"));
    // The volume comes selected; nothing to apply yet.
    EXPECT_EQ(driver->text("partition:name"), "Data");
    EXPECT_FALSE(enabled(*driver, "partition:apply"));

    driver->click("partition:add");
    EXPECT_EQ(driver->text("partition:name"), "Untitled");
    EXPECT_EQ(driver->find<dialogs::ChoiceButton>("partition:format")->currentValue(), "ext4");
    replaceText(*driver, "partition:name", "Music");
    driver->click("partition:format");
    EXPECT_TRUE(driver->choosePopupItem("exFAT"));
    ASSERT_TRUE(enabled(*driver, "partition:apply"));

    driver->click("partition:apply");
    // The summary first; nothing has been sent.
    EXPECT_TRUE(driver->shows("partition:summary:0"));
    EXPECT_NE(driver->texts("partition:summary:0").front().find("Create “Music”, exFAT"), std::string::npos);
    EXPECT_TRUE(calls().empty());
    driver->click("partition:confirm");
    ASSERT_TRUE(runs(*driver));
    EXPECT_EQ(calls(), std::vector<std::string>{sample::kSata + " CreatePartitionAndFormat"});
    EXPECT_NE(driver->text("partition:status").find("was partitioned"), std::string::npos);
}

TEST_F(PartitionTest, ResizesWithTheSizeField)
{
    auto driver = open("v:" + sample::kData);
    ASSERT_TRUE(driver);
    replaceText(*driver, "partition:size", "250");
    driver->typeKey(key::Return);
    EXPECT_EQ(driver->text("partition:size"), udisks::formatSize(250 * kGB));
    driver->click("partition:apply");
    driver->click("partition:confirm");
    ASSERT_TRUE(runs(*driver));
    EXPECT_EQ(calls(), (std::vector<std::string>{sample::kData + " Resize", sample::kData + " Resize"}));
    const auto& all = scenario.backend().calls;
    std::vector<std::string> interfaces;
    for (const udisks::Call& call : all) {
        if (call.method == "Resize")
            interfaces.push_back(call.interface);
    }
    // The filesystem shrinks before its partition.
    EXPECT_EQ(interfaces, (std::vector<std::string>{udisks::interface::kFilesystem, udisks::interface::kPartition}));
}

TEST_F(PartitionTest, DragsABoundary)
{
    auto driver = open("v:" + sample::kData);
    ASSERT_TRUE(driver);
    auto* pie = driver->find<dialogs::PieChart>("partition:pie");
    ASSERT_TRUE(pie);
    ASSERT_TRUE(pie->handlePoint(0));
    ASSERT_TRUE(pie->onBoundaryMoved);
    // The handle dragged to three quarters of the disk.
    pie->onBoundaryMoved(0, dialogs::kMiB + 750 * kGB);
    pie->onDragFinished();
    driver->layout();
    EXPECT_EQ(driver->text("partition:size"), udisks::formatSize(750 * kGB));
    EXPECT_TRUE(enabled(*driver, "partition:apply"));
    EXPECT_TRUE(enabled(*driver, "partition:revert"));
    driver->click("partition:revert");
    EXPECT_EQ(driver->text("partition:size"), udisks::formatSize(500 * kGB));
    EXPECT_FALSE(enabled(*driver, "partition:apply"));
}

TEST_F(PartitionTest, StopsAtTheFirstFailure)
{
    failing = "Resize";
    auto driver = open("v:" + sample::kData);
    ASSERT_TRUE(driver);
    replaceText(*driver, "partition:size", "250 GB");
    driver->typeKey(key::Return);
    driver->click("partition:add");
    driver->click("partition:apply");
    driver->click("partition:confirm");
    ASSERT_TRUE(runs(*driver));
    // The create after the failed shrink never ran.
    EXPECT_EQ(calls(), std::vector<std::string>{sample::kData + " Resize"});
    EXPECT_NE(driver->text("partition:error").find("Resize failed"), std::string::npos);
    EXPECT_NE(driver->text("partition:status").find("Nothing was changed"), std::string::npos);
}

TEST_F(PartitionTest, DeletesAndRecreates)
{
    auto driver = open("v:" + sample::kData);
    ASSERT_TRUE(driver);
    driver->click("partition:remove");
    EXPECT_FALSE(driver->find<View>("partition:name")->isEnabled());
    driver->click("partition:add");
    driver->click("partition:apply");
    driver->click("partition:confirm");
    ASSERT_TRUE(runs(*driver));
    EXPECT_EQ(calls(), (std::vector<std::string>{sample::kData + " Delete", sample::kSata + " CreatePartitionAndFormat"}));
    const udisks::Call& create = scenario.backend().calls.back();
    EXPECT_EQ(create.arguments.at(0).value<uint64_t>(), dialogs::kMiB);
}

TEST_F(PartitionTest, ATypedSizeStaysWithItsSlice)
{
    auto driver = open("v:" + sample::kData);
    ASSERT_TRUE(driver);
    auto* pie = driver->find<dialogs::PieChart>("partition:pie");
    ASSERT_TRUE(pie);
    // The editor numbers the slices in disk order: Data, then the free space.
    const dialogs::PartitionLayout layout = dialogs::PartitionLayout::fromDisk(scenario.model(), sample::kSata);
    const uint32_t data = layout.slices()[0].id;
    const uint32_t free = layout.slices()[1].id;

    // Into the size field and out again: nothing changes.
    driver->click("partition:size");
    driver->click("partition:name");
    EXPECT_FALSE(enabled(*driver, "partition:apply"));

    // Typed for Data, then the free space clicked: Data takes it.
    replaceText(*driver, "partition:size", "250");
    pie->onSelected(free);
    driver->layout();
    pie->onSelected(data);
    driver->layout();
    EXPECT_EQ(driver->text("partition:size"), udisks::formatSize(250 * kGB));
    EXPECT_TRUE(enabled(*driver, "partition:apply"));
}


TEST_F(PartitionTest, AFailedNewMapSaysTheDiskChanged)
{
    failing = "Format";
    auto driver = open("d:" + sample::kCard);
    ASSERT_TRUE(driver);
    driver->click("partition:add");
    driver->click("partition:apply");
    driver->click("partition:confirm");
    ASSERT_TRUE(runs(*driver));
    EXPECT_EQ(calls(), std::vector<std::string>{sample::kCard + " Format"});
    EXPECT_NE(driver->text("partition:status").find("only in part"), std::string::npos);
}

TEST_F(PartitionTest, FatNamesInCapitals)
{
    auto driver = open("v:" + sample::kData);
    ASSERT_TRUE(driver);
    driver->click("partition:add");
    driver->click("partition:format");
    EXPECT_TRUE(driver->choosePopupItem("FAT32 (MS-DOS)"));
    EXPECT_EQ(driver->text("partition:name"), "UNTITLED");
    replaceText(*driver, "partition:name", "photos");
    EXPECT_EQ(driver->text("partition:name"), "PHOTOS");
}

TEST_F(PartitionTest, MountedVolumesNeedUnmounting)
{
    auto driver = open("v:" + sample::kStickVolume);
    ASSERT_TRUE(driver);
    EXPECT_FALSE(enabled(*driver, "partition:size"));
    EXPECT_NE(driver->text("partition:note").find("Unmount “STICK”"), std::string::npos);
}

TEST_F(PartitionTest, MasterBootRecordLimit)
{
    auto driver = open("v:" + sample::kMbrLinux);
    ASSERT_TRUE(driver);
    driver->click("partition:add");
    driver->click("partition:add");
    EXPECT_FALSE(enabled(*driver, "partition:add"));
    EXPECT_EQ(driver->find<View>("partition:add")->toolTip(), "A Master Boot Record holds at most four partitions.");
}
