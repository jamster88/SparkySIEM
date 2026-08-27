/**
 * @file test_files_monitor.cpp
 * @brief Unit tests for FilesMonitor.
 *
 * FilesMonitor previously did not compile: it constructed FileMonitor with two
 * arguments where three were required. Once that was fixed it still published nothing,
 * because it created monitors without ever running their blocking monitor() loop.
 * The tests below pin both behaviours down.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "FilesMonitor.h"
#include "tests/FakeSink.h"
#include "tests/TestSupport.h"

namespace {

namespace fs = std::filesystem;
using namespace sparky::testing;

/// Short interval so the tests do not wait a second for every rescan.
constexpr std::chrono::milliseconds kFastScan{50};

/// True when @p path is in the monitor's current file list.
bool isMonitoring(const FilesMonitor& monitor, const std::string& path) {
    const auto files = monitor.monitoredFiles();
    return std::find(files.begin(), files.end(), path) != files.end();
}

TEST(FilesMonitorConstruction, IsNotCopyable) {
    static_assert(!std::is_copy_constructible<FilesMonitor>::value,
                  "FilesMonitor must not be copy constructible");
    static_assert(!std::is_copy_assignable<FilesMonitor>::value,
                  "FilesMonitor must not be copy assignable");
    SUCCEED();
}

TEST(FilesMonitorConstruction, ThrowsWhenTheSinkIsNull) {
    EXPECT_THROW(FilesMonitor({}, std::shared_ptr<MessageSink>{}, "topic", kFastScan),
                 std::invalid_argument);
}

TEST(FilesMonitorConstruction, IgnoresPathsThatDoNotExist) {
    TempDir dir;
    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.file("nope.log")}, sink, "test-topic", kFastScan);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_TRUE(monitor.monitoredFiles().empty());
    EXPECT_EQ(sink->size(), 0u);
}

TEST(FilesMonitorFiles, MonitorsEveryFileInTheList) {
    TempDir dir;
    const std::string first = dir.file("first.log");
    const std::string second = dir.file("second.log");
    writeFile(first, "");
    writeFile(second, "");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({first, second}, sink, "test-topic", kFastScan);

    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 2; }));
    EXPECT_TRUE(isMonitoring(monitor, first));
    EXPECT_TRUE(isMonitoring(monitor, second));
}

TEST(FilesMonitorFiles, PublishesChangesFromEveryMonitoredFile) {
    TempDir dir;
    const std::string first = dir.file("first.log");
    const std::string second = dir.file("second.log");
    writeFile(first, "");
    writeFile(second, "");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({first, second}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 2; }));

    appendToFile(first, "from first\n");
    appendToFile(second, "from second\n");

    // Before the fix this published nothing at all: monitor() was never called.
    ASSERT_TRUE(waitFor([&] {
        return countLine(sink->messages(), "from first") == 1 &&
               countLine(sink->messages(), "from second") == 1;
    }));
}

TEST(FilesMonitorDirectories, MonitorsFilesInsideAWatchedDirectory) {
    TempDir dir;
    const std::string inside = dir.file("app.log");
    writeFile(inside, "existing\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);

    ASSERT_TRUE(waitFor([&] { return isMonitoring(monitor, inside); }));
    EXPECT_TRUE(waitFor([&] { return countLine(sink->messages(), "existing") == 1; }));
}

TEST(FilesMonitorDirectories, PicksUpAFileCreatedAfterMonitoringStarted) {
    TempDir dir;
    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const std::string added = dir.file("late.log");
    writeFile(added, "arrived late\n");

    ASSERT_TRUE(waitFor([&] { return isMonitoring(monitor, added); }));
    EXPECT_TRUE(waitFor([&] { return countLine(sink->messages(), "arrived late") == 1; }));
}

TEST(FilesMonitorDirectories, DoesNotDescendIntoNestedDirectories) {
    TempDir dir;
    const fs::path nested = dir.path() / "nested";
    fs::create_directory(nested);
    const std::string buried = (nested / "deep.log").string();
    writeFile(buried, "deep\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(isMonitoring(monitor, buried));
}

TEST(FilesMonitorCleanup, StopsMonitoringADeletedFile) {
    TempDir dir;
    const std::string path = dir.file("transient.log");
    writeFile(path, "");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return isMonitoring(monitor, path); }));

    fs::remove(path);
    EXPECT_TRUE(waitFor([&] { return !isMonitoring(monitor, path); }));
}

TEST(FilesMonitorShutdown, DestructorStopsEveryMonitorPromptly) {
    TempDir dir;
    for (int i = 0; i < 3; ++i) {
        writeFile(dir.file("file" + std::to_string(i) + ".log"), "content\n");
    }

    auto sink = std::make_shared<FakeSink>();
    const auto start = std::chrono::steady_clock::now();
    {
        FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
        ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 3; }));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::seconds(5)) << "shutdown hung waiting on monitor threads";
    // Every monitor published its CLOSE before its thread was joined.
    EXPECT_EQ(countOfType(sink->messages(), "CLOSE"), 3u);
}

TEST(FilesMonitorShutdown, StopThenDestroyIsSafe) {
    TempDir dir;
    writeFile(dir.file("one.log"), "content\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 1; }));

    monitor.stop();
    monitor.stop();
    SUCCEED();  // the destructor runs next and must not hang or double-join
}

}  // namespace
