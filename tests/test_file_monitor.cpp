/**
 * @file test_file_monitor.cpp
 * @brief Unit tests for FileMonitor.
 *
 * Several tests here are regressions for defects found while verifying the original
 * implementation against a live Kafka broker:
 * - the whole file was resent on every change instead of just the new lines,
 * - content containing a quote or backslash produced unparseable JSON,
 * - a file that could not be opened turned the event loop into a busy loop that
 *   published roughly 1.8 million error messages in four seconds,
 * - a rotated file was never followed again, silently,
 * - there was no way to stop the loop, so CLOSE and the final flush were dead code.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 */

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include "FileMonitor.h"
#include "tests/FakeSink.h"
#include "tests/TestSupport.h"

namespace {

namespace fs = std::filesystem;
using namespace sparky::testing;

/// Builds a monitor for @p path that publishes into @p sink.
std::unique_ptr<FileMonitor> makeMonitor(const std::string& path,
                                         const std::shared_ptr<FakeSink>& sink) {
    return std::make_unique<FileMonitor>(path, sink, "test-topic");
}

// --------------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------------

TEST(FileMonitorConstruction, IsNotCopyable) {
    // The class owns file descriptors and an inotify watch; copying it would close
    // the same descriptors twice.
    static_assert(!std::is_copy_constructible<FileMonitor>::value,
                  "FileMonitor must not be copy constructible");
    static_assert(!std::is_copy_assignable<FileMonitor>::value,
                  "FileMonitor must not be copy assignable");
    SUCCEED();
}

TEST(FileMonitorConstruction, ThrowsWhenTheFileDoesNotExist) {
    TempDir dir;
    auto sink = std::make_shared<FakeSink>();
    EXPECT_THROW(makeMonitor(dir.file("missing.log"), sink), std::runtime_error);
}

TEST(FileMonitorConstruction, ThrowsWhenTheSinkIsNull) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");
    EXPECT_THROW(FileMonitor(path, std::shared_ptr<MessageSink>{}, "topic"),
                 std::invalid_argument);
}

TEST(FileMonitorConstruction, ReportsTheMonitoredPath) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");
    auto sink = std::make_shared<FakeSink>();
    EXPECT_EQ(makeMonitor(path, sink)->path(), path);
}

// --------------------------------------------------------------------------------
// Startup behaviour
// --------------------------------------------------------------------------------

TEST(FileMonitorStartup, PublishesInitMessages) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));

    ASSERT_TRUE(waitFor([&] { return sink->size() >= 2; }));
    const auto types = typesOf(sink->messages());
    EXPECT_EQ(types[0], "INIT");
    EXPECT_EQ(types[1], "INIT - FILE OPEN");
}

TEST(FileMonitorStartup, PublishesContentThatAlreadyExists) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "first\nsecond\n");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));

    ASSERT_TRUE(waitFor([&] { return contentLines(sink->messages()).size() == 2; }));
    EXPECT_EQ(contentLines(sink->messages()), (std::vector<std::string>{"first", "second"}));
}

// --------------------------------------------------------------------------------
// Incremental reading: the headline defect
// --------------------------------------------------------------------------------

TEST(FileMonitorIncremental, PublishesAnAppendedLineExactlyOnce) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "line A\n");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "line A") == 1; }));

    appendToFile(path, "line B\n");
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "line B") == 1; }));

    // The original implementation reread the file from the beginning on every event,
    // so "line A" arrived again here.
    EXPECT_EQ(countLine(sink->messages(), "line A"), 1u);
    EXPECT_EQ(countLine(sink->messages(), "line B"), 1u);
}

TEST(FileMonitorIncremental, RepeatedAppendsNeverResendEarlierLines) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "line A\n");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "line A") == 1; }));

    appendToFile(path, "line B\n");
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "line B") == 1; }));
    appendToFile(path, "line C\n");
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "line C") == 1; }));

    EXPECT_EQ(contentLines(sink->messages()),
              (std::vector<std::string>{"line A", "line B", "line C"}));
}

TEST(FileMonitorIncremental, PublishesManyLinesOnceAndInOrder) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return sink->size() >= 2; }));

    constexpr int kLineCount = 500;
    std::string block;
    std::vector<std::string> expected;
    for (int i = 0; i < kLineCount; ++i) {
        const std::string line = "event " + std::to_string(i);
        block += line + "\n";
        expected.push_back(line);
    }
    appendToFile(path, block);

    ASSERT_TRUE(waitFor([&] { return contentLines(sink->messages()).size() == kLineCount; }));
    EXPECT_EQ(contentLines(sink->messages()), expected);
}

TEST(FileMonitorIncremental, WaitsForTheNewlineBeforePublishingAPartialLine) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return sink->size() >= 2; }));

    appendToFile(path, "half a record");
    // Give the monitor time to do the wrong thing before asserting it did not.
    EXPECT_FALSE(waitFor([&] { return !contentLines(sink->messages()).empty(); },
                         std::chrono::milliseconds(500)));

    appendToFile(path, " and the rest\n");
    ASSERT_TRUE(waitFor([&] { return contentLines(sink->messages()).size() == 1; }));
    EXPECT_EQ(contentLines(sink->messages())[0], "half a record and the rest");
}

TEST(FileMonitorIncremental, PreservesEmptyLines) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return sink->size() >= 2; }));

    appendToFile(path, "a\n\nb\n");
    ASSERT_TRUE(waitFor([&] { return contentLines(sink->messages()).size() == 3; }));
    EXPECT_EQ(contentLines(sink->messages()), (std::vector<std::string>{"a", "", "b"}));
}

TEST(FileMonitorIncremental, StripsCarriageReturnsFromCrlfFiles) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return sink->size() >= 2; }));

    appendToFile(path, "windows line\r\n");
    ASSERT_TRUE(waitFor([&] { return contentLines(sink->messages()).size() == 1; }));
    EXPECT_EQ(contentLines(sink->messages())[0], "windows line");
}

// --------------------------------------------------------------------------------
// Message validity
// --------------------------------------------------------------------------------

TEST(FileMonitorMessages, ContentWithQuotesAndBackslashesStaysValidJson) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return sink->size() >= 2; }));

    const std::string nasty = R"(he said "hi" \ done)";
    appendToFile(path, nasty + "\n");

    ASSERT_TRUE(waitFor([&] { return contentLines(sink->messages()).size() == 1; }));
    // parseMessage throws if any published message is not valid JSON.
    for (const auto& message : sink->messages()) {
        ASSERT_NO_THROW(parseMessage(message)) << message;
    }
    EXPECT_EQ(contentLines(sink->messages())[0], nasty);
}

TEST(FileMonitorMessages, CarryTheMonitoredPathAndTopic) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "hello\n");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return contentLines(sink->messages()).size() == 1; }));

    const auto parsed = parseMessage(sink->messages().back());
    EXPECT_EQ(parsed.at("filePath"), path);
    EXPECT_EQ(parsed.at("kafkaTopic"), "test-topic");
}

// --------------------------------------------------------------------------------
// Truncation and rotation
// --------------------------------------------------------------------------------

TEST(FileMonitorRotation, RereadsFromTheStartAfterTruncation) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "aaaa\nbbbb\ncccc\n");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return contentLines(sink->messages()).size() == 3; }));

    // Shorter than what we already consumed, so the monitor must notice the reset.
    writeFile(path, "zzz\n");

    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "zzz") == 1; }));
    EXPECT_GE(countOfType(sink->messages(), "TRUNCATED"), 1u);
    EXPECT_EQ(countLine(sink->messages(), "aaaa"), 1u);  // not replayed
}

TEST(FileMonitorRotation, FollowsANewFileThatReplacesTheWatchedPath) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "before rotation\n");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "before rotation") == 1; }));

    // Standard log rotation: move the old file aside, create a fresh one, keep writing.
    fs::rename(path, dir.file("watch.log.1"));
    writeFile(path, "after rotation\n");
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "after rotation") == 1; }));

    appendToFile(path, "still following\n");
    // The original watch followed the old inode, so nothing below ever arrived.
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "still following") == 1; }));
    EXPECT_GE(countOfType(sink->messages(), "ROTATED"), 1u);
}

// --------------------------------------------------------------------------------
// Failure handling
// --------------------------------------------------------------------------------

TEST(FileMonitorFailures, AnUnreadableFileDoesNotFloodTheSink) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "root ignores file permissions; run the suite as a normal user";
    }

    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "readable\n");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "readable") == 1; }));

    // Write-only: modifications still fire events, but opening for read fails.
    fs::permissions(path, fs::perms::owner_write, fs::perm_options::replace);
    // Spaced out so each append is its own inotify event rather than a coalesced one.
    for (int i = 0; i < 5; ++i) {
        appendToFile(path, "invisible " + std::to_string(i) + "\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // The original loop published on the order of a million error messages in seconds.
    EXPECT_LT(sink->size(), 50u) << "monitor is flooding the sink";
    // Five failing events, but the failure is only worth reporting once.
    EXPECT_EQ(countOfType(sink->messages(), "ERROR - FILE OPEN"), 1u);

    // And it recovers once the file can be read again.
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace);
    appendToFile(path, "visible again\n");
    EXPECT_TRUE(waitFor([&] { return countLine(sink->messages(), "visible again") == 1; }));
}

TEST(FileMonitorFailures, KeepsRunningWhenThePublishFails) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "one\n");

    auto sink = std::make_shared<ThrowingSink>();
    MonitorRunner runner(std::make_unique<FileMonitor>(path, sink, "test-topic"));

    ASSERT_TRUE(waitFor([&] { return sink->attempts() >= 3; }));
    appendToFile(path, "two\n");
    ASSERT_TRUE(waitFor([&] { return sink->attempts() >= 4; }));

    EXPECT_TRUE(runner.get().isRunning());
    runner.stopAndJoin();  // must still shut down cleanly
}

// --------------------------------------------------------------------------------
// Shutdown
// --------------------------------------------------------------------------------

TEST(FileMonitorShutdown, StopReturnsPromptlyAndPublishesClose) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "content\n");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "content") == 1; }));

    const auto start = std::chrono::steady_clock::now();
    runner.stopAndJoin();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::seconds(2)) << "shutdown should not wait on file activity";
    EXPECT_EQ(typesOf(sink->messages()).back(), "CLOSE");
    EXPECT_GE(sink->flushes(), 1u) << "buffered messages must be flushed on the way out";
    EXPECT_FALSE(runner.get().isRunning());
}

TEST(FileMonitorShutdown, StopIsIdempotent) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");

    auto sink = std::make_shared<FakeSink>();
    MonitorRunner runner(makeMonitor(path, sink));
    ASSERT_TRUE(waitFor([&] { return sink->size() >= 2; }));

    runner.get().stop();
    runner.get().stop();
    runner.stopAndJoin();
    runner.stopAndJoin();
    SUCCEED();
}

TEST(FileMonitorShutdown, DestructionWithoutRunningIsSafe) {
    TempDir dir;
    const std::string path = dir.file("watch.log");
    writeFile(path, "");

    auto sink = std::make_shared<FakeSink>();
    { auto monitor = makeMonitor(path, sink); }  // never started

    EXPECT_EQ(sink->size(), 0u);
}

}  // namespace
