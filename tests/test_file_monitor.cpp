#include "FileMonitor.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using testsupport::appendToFile;
using testsupport::field;
using testsupport::isWellFormedFlatJsonObject;
using testsupport::MonitorHarness;
using testsupport::ofType;
using testsupport::RecordingSink;
using testsupport::settleAndCount;
using testsupport::TempDir;
using testsupport::writeFile;

namespace {

/// Counts how many recorded messages carry the given "type".
std::size_t countType(const std::vector<std::string>& messages, const std::string& type) {
    return ofType(messages, type).size();
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(FileMonitorConstruction, ThrowsWhenTheFileDoesNotExist) {
    TempDir dir;
    EXPECT_THROW(MonitorHarness harness(dir.file("nope.txt")), std::runtime_error);
}

TEST(FileMonitorConstruction, ThrowsWhenTheSinkIsNull) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    EXPECT_THROW(FileMonitor(path, std::unique_ptr<MessageSink>(), "t"), std::invalid_argument);
}

TEST(FileMonitorConstruction, SucceedsForAnExistingFile) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "existing\n");

    EXPECT_NO_THROW(MonitorHarness harness(path));
}

// ---------------------------------------------------------------------------
// Startup behaviour
// ---------------------------------------------------------------------------

TEST(FileMonitorStartup, SendsInitMessages) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    MonitorHarness harness(path);
    harness.start();
    ASSERT_TRUE(harness.sink().waitForType("INIT - FILE OPEN"));

    const std::vector<std::string> messages = harness.messages();
    ASSERT_GE(messages.size(), 2u);
    EXPECT_EQ(field(messages[0], "type"), "INIT");
    EXPECT_EQ(field(messages[1], "type"), "INIT - FILE OPEN");
    EXPECT_EQ(field(messages[0], "filePath"), path);
    EXPECT_EQ(field(messages[0], "kafkaTopic"), "test-topic");
}

TEST(FileMonitorStartup, DoesNotForwardContentThatAlreadyExisted) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "old one\nold two\nold three\n");

    MonitorHarness harness(path);
    harness.start();

    // Only the two INIT messages should ever appear: history is not replayed, so
    // restarting the monitor cannot re-flood the topic.
    EXPECT_EQ(settleAndCount(harness.sink()), 2u);
    EXPECT_TRUE(harness.lines("MODIFY").empty());
}

// ---------------------------------------------------------------------------
// Incremental tailing - the behaviour that was most wrong before
// ---------------------------------------------------------------------------

TEST(FileMonitorTailing, OneAppendedLineProducesExactlyOneMessage) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "old one\nold two\nold three\n");

    MonitorHarness harness(path);
    harness.start();
    appendToFile(path, "brand new\n");

    ASSERT_TRUE(harness.sink().waitForType("MODIFY"));
    EXPECT_EQ(settleAndCount(harness.sink()), 3u); // INIT, INIT - FILE OPEN, one MODIFY
    EXPECT_EQ(harness.lines("MODIFY"), std::vector<std::string>{"brand new"});
}

TEST(FileMonitorTailing, ASecondAppendDoesNotResendTheFirst) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "history\n");

    MonitorHarness harness(path);
    harness.start();

    appendToFile(path, "first\n");
    ASSERT_TRUE(harness.sink().waitForType("MODIFY", 1));
    appendToFile(path, "second\n");
    ASSERT_TRUE(harness.sink().waitForType("MODIFY", 2));

    settleAndCount(harness.sink());
    const std::vector<std::string> expected{"first", "second"};
    EXPECT_EQ(harness.lines("MODIFY"), expected);
}

TEST(FileMonitorTailing, SeveralLinesInOneWriteArriveInOrder) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    MonitorHarness harness(path);
    harness.start();
    appendToFile(path, "alpha\nbeta\ngamma\n");

    ASSERT_TRUE(harness.sink().waitForType("MODIFY", 3));
    settleAndCount(harness.sink());
    const std::vector<std::string> expected{"alpha", "beta", "gamma"};
    EXPECT_EQ(harness.lines("MODIFY"), expected);
}

TEST(FileMonitorTailing, AnIncompleteLineIsHeldBackUntilItsNewlineArrives) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    MonitorHarness harness(path);
    harness.start();

    appendToFile(path, "half a li");
    // Nothing should be emitted for a line that is not finished yet: a partial
    // line must never be shipped as if it were a whole record.
    EXPECT_EQ(settleAndCount(harness.sink()), 2u);

    appendToFile(path, "ne here\n");
    ASSERT_TRUE(harness.sink().waitForType("MODIFY"));
    settleAndCount(harness.sink());
    EXPECT_EQ(harness.lines("MODIFY"), std::vector<std::string>{"half a line here"});
}

TEST(FileMonitorTailing, StripsCarriageReturnsFromCrlfFiles) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    MonitorHarness harness(path);
    harness.start();
    appendToFile(path, "windows line\r\n");

    ASSERT_TRUE(harness.sink().waitForType("MODIFY"));
    EXPECT_EQ(harness.lines("MODIFY"), std::vector<std::string>{"windows line"});
}

TEST(FileMonitorTailing, HandlesAWriteLargerThanTheReadBuffer) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    // Comfortably more than the 64 KiB read chunk, so lines land across chunk
    // boundaries and the partial-line carry-over gets a real workout.
    constexpr int kLines = 3000;
    std::ostringstream payload;
    for (int i = 0; i < kLines; ++i) {
        payload << "line " << i << " " << std::string(40, 'x') << "\n";
    }

    MonitorHarness harness(path);
    harness.start();
    appendToFile(path, payload.str());

    ASSERT_TRUE(harness.sink().waitForType("MODIFY", kLines, std::chrono::milliseconds(20000)));
    settleAndCount(harness.sink());

    const std::vector<std::string> lines = harness.lines("MODIFY");
    ASSERT_EQ(lines.size(), static_cast<std::size_t>(kLines)); // no duplicates, none lost
    for (int i = 0; i < kLines; ++i) {
        ASSERT_EQ(lines[static_cast<std::size_t>(i)],
                  "line " + std::to_string(i) + " " + std::string(40, 'x'))
            << "at index " << i;
    }
}

TEST(FileMonitorTailing, EmitsValidJsonForContentFullOfQuotesAndBackslashes) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    MonitorHarness harness(path);
    harness.start();
    const std::string hostile = "he said \"hello\" \\ tab\there";
    appendToFile(path, hostile + "\n");

    ASSERT_TRUE(harness.sink().waitForType("MODIFY"));
    const std::vector<std::string> modifies = ofType(harness.messages(), "MODIFY");
    ASSERT_EQ(modifies.size(), 1u);
    EXPECT_TRUE(isWellFormedFlatJsonObject(modifies[0])) << modifies[0];
    EXPECT_EQ(field(modifies[0], "message"), hostile);
}

// ---------------------------------------------------------------------------
// Truncation and rotation
// ---------------------------------------------------------------------------

TEST(FileMonitorTruncation, ReportsTruncationAndThenReadsFromTheStart) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "a longer original body\n");

    MonitorHarness harness(path);
    harness.start();
    writeFile(path, "short\n"); // Truncate in place, same inode.

    ASSERT_TRUE(harness.sink().waitForType("MODIFY"));
    settleAndCount(harness.sink());

    const std::vector<std::string> messages = harness.messages();
    EXPECT_EQ(countType(messages, "TRUNCATE"), 1u);
    // The replacement content is delivered once, and the old body is not replayed.
    EXPECT_EQ(harness.lines("MODIFY"), std::vector<std::string>{"short"});
}

TEST(FileMonitorRotation, PicksUpTheReplacementFileAfterALogRotation) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "before rotation\n");

    MonitorHarness harness(path);
    harness.start();

    ASSERT_EQ(std::rename(path.c_str(), (path + ".1").c_str()), 0);
    writeFile(path, "after rotation\n");

    ASSERT_TRUE(harness.sink().waitForType("MODIFY"));
    settleAndCount(harness.sink());

    const std::vector<std::string> messages = harness.messages();
    EXPECT_GE(countType(messages, "ROTATE"), 1u);
    EXPECT_GE(countType(messages, "REWATCH"), 1u);
    EXPECT_EQ(harness.lines("MODIFY"), std::vector<std::string>{"after rotation"});
}

TEST(FileMonitorRotation, KeepsFollowingThePathNotTheOldInode) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    MonitorHarness harness(path);
    harness.start();

    const std::string rotated = path + ".1";
    ASSERT_EQ(std::rename(path.c_str(), rotated.c_str()), 0);
    writeFile(path, "");
    ASSERT_TRUE(harness.sink().waitForType("REWATCH"));

    // Writes to the rotated-away file must not be reported any more...
    appendToFile(rotated, "into the old file\n");
    settleAndCount(harness.sink());
    for (const std::string& line : harness.lines("MODIFY")) {
        EXPECT_NE(line, "into the old file");
    }

    // ...while writes to the current path still are.
    appendToFile(path, "into the new file\n");
    ASSERT_TRUE(harness.sink().waitForType("MODIFY"));
    settleAndCount(harness.sink());
    EXPECT_EQ(harness.lines("MODIFY"), std::vector<std::string>{"into the new file"});
}

TEST(FileMonitorRotation, HandlesAFileMovedIntoPlaceOverTheWatchedPath) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    const std::string staged = dir.file("staged.txt");
    writeFile(path, "original\n");

    MonitorHarness harness(path);
    harness.start();

    writeFile(staged, "replacement\n");
    ASSERT_EQ(std::rename(staged.c_str(), path.c_str()), 0); // Atomic replace.

    ASSERT_TRUE(harness.sink().waitForType("MODIFY"));
    settleAndCount(harness.sink());

    EXPECT_GE(countType(harness.messages(), "ROTATE"), 1u);
    EXPECT_EQ(harness.lines("MODIFY"), std::vector<std::string>{"replacement"});
}

// ---------------------------------------------------------------------------
// Failure handling
// ---------------------------------------------------------------------------

TEST(FileMonitorFailures, AnUnreadableFileReportsOnceAndDoesNotSpin) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "running as root, which bypasses the file permissions this test needs";
    }

    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    MonitorHarness harness(path);
    harness.start();

    // Permissions are checked at open(), so a stream opened beforehand can still
    // write to a file that the monitor can no longer read.
    std::ofstream writer(path, std::ios::binary | std::ios::app);
    ASSERT_TRUE(writer.is_open());
    ASSERT_EQ(chmod(path.c_str(), 0000), 0);

    writer << "hidden line\n";
    writer.flush();

    // A single unreadable event used to be retried forever; the count must stay
    // small and the loop must remain responsive.
    const std::size_t total = settleAndCount(harness.sink());
    EXPECT_LT(total, 20u) << "monitor appears to be spinning on the failed read";
    EXPECT_GE(countType(harness.messages(), "ERROR - FILE OPEN"), 1u);

    // Once the file is readable again, the bytes written while it was not are
    // still delivered - a transient read failure must not lose data.
    ASSERT_EQ(chmod(path.c_str(), 0644), 0);
    appendToFile(path, "visible again\n");

    ASSERT_TRUE(harness.sink().waitForType("MODIFY", 2));
    const std::vector<std::string> expected{"hidden line", "visible again"};
    EXPECT_EQ(harness.lines("MODIFY"), expected);
}

TEST(FileMonitorFailures, ReportsAnErrorWhenTheFileVanishes) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "content\n");

    MonitorHarness harness(path);
    harness.start();
    ASSERT_EQ(std::remove(path.c_str()), 0);

    ASSERT_TRUE(harness.sink().waitForType("ROTATE"));
    const std::size_t total = settleAndCount(harness.sink());
    EXPECT_LT(total, 20u) << "monitor appears to be spinning on the missing file";
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

TEST(FileMonitorShutdown, StopReturnsPromptlyAndSendsCloseThenFlushes) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    MonitorHarness harness(path);
    harness.start();

    const auto before = std::chrono::steady_clock::now();
    harness.stopAndJoin();
    const auto elapsed = std::chrono::steady_clock::now() - before;

    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 2000)
        << "stop() should wake the poll() immediately";

    const std::vector<std::string> messages = harness.messages();
    ASSERT_FALSE(messages.empty());
    EXPECT_EQ(field(messages.back(), "type"), "CLOSE");
    EXPECT_EQ(harness.sink().flushCount(), 1u);
}

TEST(FileMonitorShutdown, DeliversLinesWrittenJustBeforeStopping) {
    TempDir dir;
    const std::string path = dir.file("f.txt");
    writeFile(path, "");

    MonitorHarness harness(path);
    harness.start();

    appendToFile(path, "last gasp\n");
    harness.stopAndJoin(); // Without waiting for the inotify event first.

    const std::vector<std::string> lines = harness.lines("MODIFY");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "last gasp");
}
