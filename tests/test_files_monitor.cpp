/**
 * @file test_files_monitor.cpp
 * @brief Unit tests for FilesMonitor, the group monitor.
 *
 * FilesMonitor previously did not compile: it constructed FileMonitor with two
 * arguments where three were required. Once that was fixed it still published nothing,
 * because it created monitors without ever running their blocking monitor() loop.
 * The tests below pin both behaviours down, along with the group-level guarantees the
 * single-file tests cannot cover: several files followed at once through one shared
 * sink, files appearing and disappearing under a watched directory, one bad file not
 * taking the others down, and a shutdown that stops every monitor it started.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "FilesMonitor.h"
#include "tests/FakeSink.h"
#include "tests/TestSupport.h"

namespace {

namespace fs = std::filesystem;
using namespace sparky::testing;

/// Short interval so the tests do not wait a second for every rescan.
constexpr std::chrono::milliseconds kFastScan{50};

/// Long enough for several scans to have happened, used to assert an absence.
constexpr std::chrono::milliseconds kSeveralScans{300};

/// True when @p path is in the monitor's current file list.
bool isMonitoring(const FilesMonitor& monitor, const std::string& path) {
    const auto files = monitor.monitoredFiles();
    return std::find(files.begin(), files.end(), path) != files.end();
}

/**
 * @brief Redirects std::cerr into a buffer for the lifetime of the object.
 *
 * The scanning thread reports an unwatchable path on stderr, and the point of the test
 * that uses this is that it reports it once rather than once per scan. Read text() only
 * once the monitor is gone, so no other thread is writing while the buffer is read.
 */
class CerrCapture {
public:
    CerrCapture() : previous(std::cerr.rdbuf(buffer.rdbuf())) {}
    ~CerrCapture() { std::cerr.rdbuf(previous); }

    CerrCapture(const CerrCapture&) = delete;
    CerrCapture& operator=(const CerrCapture&) = delete;

    /// Everything written to std::cerr so far.
    std::string text() const { return buffer.str(); }

private:
    std::ostringstream buffer;
    std::streambuf* previous;
};

/// How many times @p needle occurs in @p haystack.
std::size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

// --------------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------------

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

    std::this_thread::sleep_for(kSeveralScans);
    EXPECT_TRUE(monitor.monitoredFiles().empty());
    EXPECT_EQ(sink->size(), 0u);
}

TEST(FilesMonitorConstruction, AcceptsAnEmptyPathList) {
    auto sink = std::make_shared<FakeSink>();
    {
        // Nothing to watch is a legitimate state, not an error: the scanning thread
        // still has to start and shut down cleanly.
        FilesMonitor monitor({}, sink, "test-topic", kFastScan);
        std::this_thread::sleep_for(kSeveralScans);
        EXPECT_TRUE(monitor.monitoredFiles().empty());
    }
    EXPECT_EQ(sink->size(), 0u);
}

// --------------------------------------------------------------------------------
// Paths given on the command line
// --------------------------------------------------------------------------------

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

TEST(FilesMonitorFiles, MonitorsAMixOfFilesAndDirectories) {
    TempDir watched;
    TempDir elsewhere;
    const std::string inDirectory = watched.file("app.log");
    const std::string standalone = elsewhere.file("explicit.log");
    writeFile(inDirectory, "from the directory\n");
    writeFile(standalone, "named on its own\n");

    auto sink = std::make_shared<FakeSink>();
    // The command line takes both kinds of path in one list; neither shadows the other.
    FilesMonitor monitor({watched.path().string(), standalone}, sink, "test-topic", kFastScan);

    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 2; }));
    EXPECT_TRUE(isMonitoring(monitor, inDirectory));
    EXPECT_TRUE(isMonitoring(monitor, standalone));
    EXPECT_TRUE(waitFor([&] {
        return countLine(sink->messages(), "from the directory") == 1 &&
               countLine(sink->messages(), "named on its own") == 1;
    }));
}

TEST(FilesMonitorFiles, ForwardsOnlyNewLinesFromEachFile) {
    TempDir dir;
    const std::string first = dir.file("first.log");
    const std::string second = dir.file("second.log");
    writeFile(first, "first start\n");
    writeFile(second, "second start\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({first, second}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 2; }));

    appendToFile(first, "first next\n");
    appendToFile(second, "second next\n");
    appendToFile(first, "first last\n");

    // The whole-file-resend defect showed up here as repeated earlier lines, and every
    // monitor in the group has to be free of it, not just the first one started.
    ASSERT_TRUE(waitFor([&] {
        return contentLines(messagesFor(sink->messages(), first)).size() == 3 &&
               contentLines(messagesFor(sink->messages(), second)).size() == 2;
    }));

    const auto messages = sink->messages();
    EXPECT_EQ(contentLines(messagesFor(messages, first)),
              (std::vector<std::string>{"first start", "first next", "first last"}));
    EXPECT_EQ(contentLines(messagesFor(messages, second)),
              (std::vector<std::string>{"second start", "second next"}));
}

TEST(FilesMonitorFiles, KeepsEachFilesMessagesUnderItsOwnPath) {
    TempDir dir;
    const std::string first = dir.file("first.log");
    const std::string second = dir.file("second.log");
    writeFile(first, "belongs to first\n");
    writeFile(second, "belongs to second\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({first, second}, sink, "test-topic", kFastScan);

    ASSERT_TRUE(waitFor([&] {
        return countLine(sink->messages(), "belongs to first") == 1 &&
               countLine(sink->messages(), "belongs to second") == 1;
    }));

    // One sink is shared by every monitor, so the only thing telling a consumer which
    // file a line came from is the filePath field.
    for (const auto& message : sink->messages()) {
        const auto parsed = parseMessage(message);
        const auto path = parsed.at("filePath").get<std::string>();
        const auto body = parsed.at("message").get<std::string>();
        EXPECT_TRUE(path == first || path == second) << "unexpected path " << path;
        if (body == "belongs to first") {
            EXPECT_EQ(path, first);
        } else if (body == "belongs to second") {
            EXPECT_EQ(path, second);
        }
        EXPECT_EQ(parsed.at("kafkaTopic"), "test-topic");
    }
}

TEST(FilesMonitorFiles, DoesNotMonitorAFileTwiceWhenItsDirectoryIsAlsoListed) {
    TempDir dir;
    const std::string inside = dir.file("app.log");
    writeFile(inside, "only once please\n");

    auto sink = std::make_shared<FakeSink>();
    // Both paths resolve to the same file; a second monitor on it would double every line.
    FilesMonitor monitor({dir.path().string(), inside}, sink, "test-topic", kFastScan);

    ASSERT_TRUE(waitFor([&] { return isMonitoring(monitor, inside); }));
    std::this_thread::sleep_for(kSeveralScans);

    EXPECT_EQ(monitor.monitoredFiles().size(), 1u);
    EXPECT_EQ(countLine(sink->messages(), "only once please"), 1u);
}

TEST(FilesMonitorFiles, MonitorsAFileThatAppearsAfterItWasListed) {
    TempDir dir;
    const std::string later = dir.file("not-yet.log");

    auto sink = std::make_shared<FakeSink>();
    // A named file that does not exist yet is not an error; log files get created late.
    FilesMonitor monitor({later}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().empty(); }));

    writeFile(later, "here at last\n");

    ASSERT_TRUE(waitFor([&] { return isMonitoring(monitor, later); }));
    EXPECT_TRUE(waitFor([&] { return countLine(sink->messages(), "here at last") == 1; }));
}

// --------------------------------------------------------------------------------
// Directories
// --------------------------------------------------------------------------------

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

    std::this_thread::sleep_for(kSeveralScans);
    EXPECT_FALSE(isMonitoring(monitor, buried));
}

TEST(FilesMonitorDirectories, SkipsEntriesThatAreNotRegularFiles) {
    TempDir dir;
    const std::string pipe = dir.file("a.fifo");
    ASSERT_EQ(mkfifo(pipe.c_str(), 0644), 0) << "could not create the test FIFO";
    const std::string regular = dir.file("b.log");
    writeFile(regular, "an ordinary file\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);

    // The regular file proves the directory really was scanned; opening the FIFO would
    // have blocked the monitor thread on a reader that never arrives.
    ASSERT_TRUE(waitFor([&] { return isMonitoring(monitor, regular); }));
    std::this_thread::sleep_for(kSeveralScans);
    EXPECT_FALSE(isMonitoring(monitor, pipe));
    EXPECT_EQ(monitor.monitoredFiles().size(), 1u);
}

TEST(FilesMonitorDirectories, PublishesValidJsonForAwkwardFileNames) {
    TempDir dir;
    // Legal on Linux, and fatal to a message built by string concatenation without
    // escaping: the path is one of the five fields that has to survive it.
    const std::string awkward = dir.file("he said \"hi\".log");
    writeFile(awkward, "quoted name\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);

    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "quoted name") == 1; }));
    for (const auto& message : sink->messages()) {
        EXPECT_NO_THROW(parseMessage(message)) << message;
        EXPECT_EQ(parseMessage(message).at("filePath"), awkward);
    }
}

TEST(FilesMonitorDirectories, MonitorsEveryFileInABusyDirectory) {
    TempDir dir;
    constexpr int kFileCount = 12;
    for (int i = 0; i < kFileCount; ++i) {
        writeFile(dir.file("file" + std::to_string(i) + ".log"),
                  "line from " + std::to_string(i) + "\n");
    }

    auto sink = std::make_shared<FakeSink>();
    {
        FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
        ASSERT_TRUE(waitFor(
            [&] { return monitor.monitoredFiles().size() == std::size_t(kFileCount); }));
        ASSERT_TRUE(waitFor([&] {
            return contentLines(sink->messages()).size() == std::size_t(kFileCount);
        }));

        for (int i = 0; i < kFileCount; ++i) {
            EXPECT_EQ(countLine(sink->messages(), "line from " + std::to_string(i)), 1u);
        }
    }
    // Every one of them was shut down, not just the ones the scan happened to reach.
    EXPECT_EQ(countOfType(sink->messages(), "CLOSE"), std::size_t(kFileCount));
}

// --------------------------------------------------------------------------------
// Failure handling
// --------------------------------------------------------------------------------

TEST(FilesMonitorFailures, KeepsMonitoringTheOtherFilesWhenOneCannotBeWatched) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "root ignores file permissions; run the suite as a normal user";
    }

    TempDir dir;
    const std::string unwatchable = dir.file("locked.log");
    const std::string readable = dir.file("open.log");
    writeFile(unwatchable, "cannot be read\n");
    writeFile(readable, "can be read\n");
    // inotify needs read permission to add a watch, so this one fails in the FileMonitor
    // constructor. It must not abort the rest of the scan.
    fs::permissions(unwatchable, fs::perms::none, fs::perm_options::replace);

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);

    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "can be read") == 1; }));
    std::this_thread::sleep_for(kSeveralScans);
    EXPECT_FALSE(isMonitoring(monitor, unwatchable));
    EXPECT_TRUE(isMonitoring(monitor, readable));
    EXPECT_EQ(countLine(sink->messages(), "cannot be read"), 0u);
}

TEST(FilesMonitorFailures, ReportsAnUnwatchableFileOnlyOnce) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "root ignores file permissions; run the suite as a normal user";
    }

    TempDir dir;
    const std::string unwatchable = dir.file("locked.log");
    writeFile(unwatchable, "cannot be read\n");
    fs::permissions(unwatchable, fs::perms::none, fs::perm_options::replace);

    auto sink = std::make_shared<FakeSink>();
    std::string reported;
    {
        CerrCapture captured;
        {
            FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
            // Several scans, each of which retries the file and fails again.
            std::this_thread::sleep_for(kSeveralScans);
        }
        reported = captured.text();
    }

    // Retrying is fine; saying so on every scan is how a log file fills a disk.
    EXPECT_EQ(countOccurrences(reported, unwatchable), 1u) << reported;
}

TEST(FilesMonitorFailures, ReportsAnUnwatchableFileAgainAfterItComesBack) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "root ignores file permissions; run the suite as a normal user";
    }

    TempDir dir;
    const std::string unwatchable = dir.file("locked.log");

    auto sink = std::make_shared<FakeSink>();
    std::string reported;
    {
        CerrCapture captured;
        {
            FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);

            writeFile(unwatchable, "cannot be read\n");
            fs::permissions(unwatchable, fs::perms::none, fs::perm_options::replace);
            std::this_thread::sleep_for(kSeveralScans);  // reported once

            fs::remove(unwatchable);
            std::this_thread::sleep_for(kSeveralScans);  // the failure is forgotten

            writeFile(unwatchable, "still cannot be read\n");
            fs::permissions(unwatchable, fs::perms::none, fs::perm_options::replace);
            std::this_thread::sleep_for(kSeveralScans);  // and reported again
        }
        reported = captured.text();
    }

    // A path that goes away has to drop out of the set of already-reported failures,
    // otherwise the set grows for the life of the process and a file that comes back
    // broken is never mentioned again.
    EXPECT_EQ(countOccurrences(reported, unwatchable), 2u) << reported;
}

TEST(FilesMonitorFailures, KeepsRunningWhenThePublishFails) {
    TempDir dir;
    const std::string first = dir.file("first.log");
    const std::string second = dir.file("second.log");
    writeFile(first, "one\n");
    writeFile(second, "two\n");

    auto sink = std::make_shared<ThrowingSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);

    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 2; }));
    ASSERT_TRUE(waitFor([&] { return sink->attempts() >= 6; }));

    // A sink that is refusing everything must not cost us the monitors.
    appendToFile(first, "three\n");
    const std::size_t before = sink->attempts();
    EXPECT_TRUE(waitFor([&] { return sink->attempts() > before; }));
    EXPECT_EQ(monitor.monitoredFiles().size(), 2u);
}

// --------------------------------------------------------------------------------
// Files coming and going
// --------------------------------------------------------------------------------

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

TEST(FilesMonitorCleanup, PublishesCloseForADeletedFile) {
    TempDir dir;
    const std::string going = dir.file("going.log");
    const std::string staying = dir.file("staying.log");
    writeFile(going, "here for now\n");
    writeFile(staying, "here to stay\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 2; }));

    fs::remove(going);

    // The monitor is shut down properly rather than abandoned, so its sink is flushed.
    ASSERT_TRUE(waitFor([&] {
        return countOfType(messagesFor(sink->messages(), going), "CLOSE") == 1;
    }));
    EXPECT_EQ(countOfType(messagesFor(sink->messages(), staying), "CLOSE"), 0u);
    EXPECT_TRUE(isMonitoring(monitor, staying));
}

TEST(FilesMonitorCleanup, ADeletedFilesShutdownDoesNotBlockTheGroup) {
    TempDir dir;
    const std::string going = dir.file("going.log");
    const std::string staying = dir.file("staying.log");
    writeFile(going, "here for now\n");
    writeFile(staying, "here to stay\n");

    constexpr std::chrono::milliseconds kFlushCost{400};
    auto sink = std::make_shared<SlowFlushSink>(kFlushCost);
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 2; }));

    fs::remove(going);

    // Winding a monitor down costs whatever the sink takes to drain, and a real sink
    // takes up to a second. Doing that with the monitor lock held stalls every other
    // caller for the same time, including stop().
    std::chrono::steady_clock::duration worstCall{};
    bool removed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!removed && std::chrono::steady_clock::now() < deadline) {
        const auto before = std::chrono::steady_clock::now();
        const auto files = monitor.monitoredFiles();
        worstCall = std::max(worstCall, std::chrono::steady_clock::now() - before);
        removed = std::find(files.begin(), files.end(), going) == files.end();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(removed) << "the deleted file was never dropped";
    EXPECT_LT(worstCall, kFlushCost / 2) << "a query waited on the deleted file's shutdown";
}

TEST(FilesMonitorCleanup, MonitorsAFileRecreatedAtTheSamePath) {
    TempDir dir;
    const std::string path = dir.file("recreated.log");
    writeFile(path, "first life\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "first life") == 1; }));

    fs::remove(path);
    ASSERT_TRUE(waitFor([&] { return !isMonitoring(monitor, path); }));

    writeFile(path, "second life\n");

    ASSERT_TRUE(waitFor([&] { return isMonitoring(monitor, path); }));
    EXPECT_TRUE(waitFor([&] { return countLine(sink->messages(), "second life") == 1; }));
    EXPECT_EQ(countLine(sink->messages(), "first life"), 1u);
}

TEST(FilesMonitorCleanup, RepublishesTheRotatedCopyInsideAWatchedDirectory) {
    TempDir dir;
    const std::string path = dir.file("app.log");
    writeFile(path, "before rotation\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "before rotation") == 1; }));

    fs::rename(path, dir.file("app.log.1"));
    writeFile(path, "after rotation\n");

    // The new app.log is followed, which is the part that matters.
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "after rotation") == 1; }));

    // This pins a documented limitation rather than a desired behaviour: to the directory
    // scan app.log.1 is simply a new file, so its contents go out a second time. Fixing
    // it needs per-inode bookkeeping of what has already been consumed; when that lands,
    // this expectation becomes 1.
    ASSERT_TRUE(waitFor([&] { return countLine(sink->messages(), "before rotation") == 2; }));
    EXPECT_TRUE(isMonitoring(monitor, dir.file("app.log.1")));
}

// --------------------------------------------------------------------------------
// Shutdown
// --------------------------------------------------------------------------------

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

TEST(FilesMonitorShutdown, StopPreventsAnyFurtherFileFromBeingPickedUp) {
    TempDir dir;
    writeFile(dir.file("present.log"), "already here\n");

    auto sink = std::make_shared<FakeSink>();
    FilesMonitor monitor({dir.path().string()}, sink, "test-topic", kFastScan);
    ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 1; }));

    monitor.stop();

    // A scan that started new monitors after stop() would leave threads publishing that
    // stop() has already walked past; the destructor would be the only thing left to
    // catch them.
    const std::string added = dir.file("after-stop.log");
    writeFile(added, "must not be forwarded\n");
    std::this_thread::sleep_for(kSeveralScans);

    EXPECT_FALSE(isMonitoring(monitor, added));
    EXPECT_EQ(countLine(sink->messages(), "must not be forwarded"), 0u);
}

TEST(FilesMonitorShutdown, StopCutsALongScanIntervalShort) {
    TempDir dir;
    writeFile(dir.file("one.log"), "content\n");

    auto sink = std::make_shared<FakeSink>();
    const auto start = std::chrono::steady_clock::now();
    {
        // The scanning thread spends nearly all of its life asleep between scans, so
        // shutdown has to interrupt that sleep rather than wait it out.
        FilesMonitor monitor({dir.path().string()}, sink, "test-topic",
                             std::chrono::seconds(30));
        ASSERT_TRUE(waitFor([&] { return monitor.monitoredFiles().size() == 1; }));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::seconds(5)) << "shutdown waited out the scan interval";
    EXPECT_EQ(countOfType(sink->messages(), "CLOSE"), 1u);
}

}  // namespace
