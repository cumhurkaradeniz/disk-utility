// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "fake_udisks.h"

#include "application.h"
#include "dialogs/volume_format.h"
#include "images/formats.h"
#include "images/imagefile.h"
#include "images/progress.h"
#include "images/size.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

using namespace images;
using namespace std::chrono_literals;

TEST(SizeTest, ImageSizesAreWholeMebibytes)
{
    EXPECT_EQ(dialogs::alignUp(100'000'000), 96ull << 20);
    EXPECT_EQ(unitName(SizeUnit::Gigabytes), "GB");
    EXPECT_EQ(unitBytes(SizeUnit::Terabytes), 1'000'000'000'000u);
}

TEST(ImageFileTest, Extensions)
{
    EXPECT_TRUE(isDiskImageFile("/a/b.img"));
    EXPECT_TRUE(isDiskImageFile("/a/b.ISO"));
    EXPECT_TRUE(isDiskImageFile("b.raw"));
    EXPECT_FALSE(isDiskImageFile("b.img.xz"));
    EXPECT_FALSE(isDiskImageFile("img"));
    EXPECT_TRUE(attachesReadOnly("debian.iso"));
    EXPECT_TRUE(attachesReadOnly("Disc.Iso"));
    EXPECT_FALSE(attachesReadOnly("disk.img"));
    EXPECT_FALSE(attachesReadOnly("disk.raw"));
}

TEST(ImageFileTest, FileNames)
{
    EXPECT_EQ(imageFileName("Backup"), "Backup.img");
    EXPECT_EQ(imageFileName("  Backup  "), "Backup.img");
    EXPECT_EQ(imageFileName("disc.iso"), "disc.iso");
    EXPECT_EQ(imageFileName("my.disk"), "my.disk.img");
    EXPECT_EQ(imageFileName(""), "");
    EXPECT_EQ(imageFileName(" "), "");
    EXPECT_EQ(imageFileName(".."), "");
    EXPECT_EQ(imageFileName("a/b"), "");
}

class TempFolder
{
public:
    TempFolder()
        : path(std::filesystem::temp_directory_path()
               / ("disk-utility-images-" + std::to_string(::getpid()) + "-" + std::to_string(counter()++)))
    {
        std::filesystem::create_directories(path);
    }
    ~TempFolder() { std::filesystem::remove_all(path); }

    std::string file(const std::string& name) const { return (path / name).string(); }

    std::filesystem::path path;

private:
    static int& counter()
    {
        static int value = 0;
        return value;
    }
};

TEST(ImageFileTest, BlankFilesTakeNoRoom)
{
    TempFolder folder;
    const std::string path = folder.file("blank.img");
    EXPECT_EQ(createBlankFile(path, 64ull << 20), "");
    struct stat info {};
    ASSERT_EQ(::stat(path.c_str(), &info), 0);
    EXPECT_EQ(uint64_t(info.st_size), 64ull << 20);
    EXPECT_LT(uint64_t(info.st_blocks) * 512, 1ull << 20);

    // An existing file is kept as it is.
    std::ofstream(folder.file("taken.img")) << "data";
    EXPECT_NE(createBlankFile(folder.file("taken.img"), 1 << 20), "");
    EXPECT_EQ(std::filesystem::file_size(folder.file("taken.img")), 4u);

    EXPECT_NE(createBlankFile(folder.file("missing/blank.img"), 1 << 20), "");
    // More than any disk here has free.
    EXPECT_NE(createBlankFile(folder.file("huge.img"), 1ull << 62), "");
    EXPECT_FALSE(std::filesystem::exists(folder.file("huge.img")));
}

TEST(ImageFileTest, LoopOfFile)
{
    udisks::Model model;
    udisks::fake::loadSample(model);
    EXPECT_EQ(loopOfFile(model, "/home/u/Downloads/debian.iso"), udisks::fake::sample::kImage);
    EXPECT_EQ(loopOfFile(model, "/home/u/Downloads/../Downloads/debian.iso"), udisks::fake::sample::kImage);
    EXPECT_EQ(loopOfFile(model, "/home/u/other.iso"), "");
    EXPECT_EQ(loopOfFile(model, ""), "");
}

TEST(FormatsTest, PartitionTypes)
{
    EXPECT_EQ(tableType(Scheme::Gpt), "gpt");
    EXPECT_EQ(tableType(Scheme::Mbr), "dos");
    EXPECT_EQ(tableType(Scheme::None), "");
    EXPECT_EQ(dialogs::partitionTypeFor("gpt", "ext4", false), "0fc63daf-8483-4772-8e79-3d69d8477de4");
    EXPECT_EQ(dialogs::partitionTypeFor("gpt", "exfat", false), "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
    EXPECT_EQ(dialogs::partitionTypeFor("gpt", "exfat", true), "0fc63daf-8483-4772-8e79-3d69d8477de4");
    EXPECT_EQ(dialogs::partitionTypeFor("dos", "vfat", false), "0x0c");
    EXPECT_EQ(dialogs::partitionTypeFor("dos", "ntfs", false), "0x07");
    EXPECT_EQ(dialogs::partitionTypeFor("dos", "xfs", false), "0x83");
    EXPECT_EQ(dialogs::partitionTypeFor("", "ext4", false), "");
}

TEST(ProgressTest, RateOverTheLastSeconds)
{
    RateMeter meter;
    const RateMeter::Clock::time_point start{};
    meter.start(start);
    EXPECT_EQ(meter.rate(), 0);
    EXPECT_FALSE(meter.remaining(100));
    meter.add(start + 500ms, 50);
    // Not a second yet.
    EXPECT_EQ(meter.rate(), 0);
    meter.add(start + 2s, 200);
    EXPECT_DOUBLE_EQ(meter.rate(), 100);
    EXPECT_EQ(meter.remaining(1200), 10s);

    // Slower from here: after the window only the slow part counts.
    for (int second = 3; second <= 12; ++second)
        meter.add(start + std::chrono::seconds(second), 200 + uint64_t(second - 2) * 10);
    EXPECT_NEAR(meter.rate(), 10, 0.01);
    EXPECT_EQ(meter.remaining(400), 10s);
    EXPECT_EQ(meter.remaining(300), 0s);
    EXPECT_EQ(meter.remaining(100), 0s);
}

TEST(ProgressTest, Texts)
{
    EXPECT_EQ(timeLeftText(0s), "About 5 seconds left");
    EXPECT_EQ(timeLeftText(12s), "About 15 seconds left");
    EXPECT_EQ(timeLeftText(59s), "About 60 seconds left");
    EXPECT_EQ(timeLeftText(60s), "About 1 minute left");
    EXPECT_EQ(timeLeftText(150s), "About 3 minutes left");
    EXPECT_EQ(timeLeftText(89min), "About 89 minutes left");
    EXPECT_EQ(timeLeftText(90min), "About 2 hours left");
    EXPECT_EQ(timeLeftText(5h), "About 5 hours left");
    EXPECT_EQ(rateText(0), "");
    EXPECT_NE(rateText(35'100'000).find("MB/s"), std::string::npos);
    EXPECT_NE(amountText(1, 2).find(" of "), std::string::npos);
    EXPECT_EQ(progressText(1, 2, 0), amountText(1, 2));
    EXPECT_NE(progressText(1, 2, 1000).find("("), std::string::npos);
}

TEST(ApplicationTest, OpensPathsAndFileUrls)
{
    Application application(std::make_unique<udisks::fake::FakeBackend>());
    std::vector<std::vector<std::string>> opened;
    auto subscription = application.openRequested.connect(
        [&](const std::vector<std::string>& paths) { opened.push_back(paths); });

    application.open({"file:///home/u/My%20Disk.img", "/home/u/./b.iso", "relative.img", "http://host/c.img"});
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], (std::vector<std::string>{"/home/u/My Disk.img", "/home/u/b.iso"}));

    application.open({"relative.img", "../up.img"}, "/home/u/Downloads");
    ASSERT_EQ(opened.size(), 2u);
    EXPECT_EQ(opened[1], (std::vector<std::string>{"/home/u/Downloads/relative.img", "/home/u/up.img"}));

    // Nothing left to open.
    application.open({"relative.img"});
    application.open({"relative.img"}, "also/relative");
    EXPECT_EQ(opened.size(), 2u);
}
