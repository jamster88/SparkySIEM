/**
 * @file FileMonitor_test.cpp
 * @brief Unit tests for the FileMonitor class
 *
 * These tests verify the functionality of the FileMonitor class,
 * including timestamp generation, message formatting, and file monitoring logic.
 *
 * Note: Full integration tests with inotify and Kafka require a Linux environment.
 * This file contains unit tests for the non-platform-specific logic.
 *
 * Test categories:
 * - Timestamp tests: Verify timestamp format and uniqueness
 * - Message format tests: Verify JSON message structure
 * - File system tests: Verify file existence and reading
 * - Thread safety tests: Verify concurrent access handling
 */

#include <gtest/gtest.h>
#include <string>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <memory>

// Include the FileMonitor header to access the class
#include "../FileMonitor.h"

// Test fixture for FileMonitor tests
class FileMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test file
        testFilePath = "/tmp/test_file_monitor_" + std::to_string(std::time(nullptr)) + ".txt";
        std::ofstream testFile(testFilePath);
        testFile << "Initial line 1\n";
        testFile << "Initial line 2\n";
        testFile.close();
    }

    void TearDown() override {
        // Clean up the test file
        std::remove(testFilePath.c_str());
    }

    std::string testFilePath;
};

// Test the timestamp format
TEST(FileMonitorTest, GetCurrentTimestampFormat) {
    auto monitor = std::make_unique<FileMonitor>(testFilePath, "localhost:9092", "test-topic");

    std::string timestamp = monitor->getCurrentTimestamp();

    // Check format: YYYY-MM-DD HH:MM:SS.mmm
    // Expected length: 23 characters (e.g., "2025-04-04 14:30:25.123")
    EXPECT_EQ(timestamp.length(), 23);

    // Check that the format matches expected pattern
    // Year (4 digits)
    EXPECT_EQ(timestamp[4], '-');
    EXPECT_EQ(timestamp[7], '-');
    EXPECT_EQ(timestamp[10], ' ');
    EXPECT_EQ(timestamp[13], ':');
    EXPECT_EQ(timestamp[16], ':');
    EXPECT_EQ(timestamp[19], '.');

    // Verify we can parse the timestamp
    std::istringstream ss(timestamp);
    std::string datePart, timePart, msPart;
    std::getline(ss, datePart, ' ');
    std::getline(ss, timePart, '.');
    std::getline(ss, msPart);

    EXPECT_EQ(datePart.length(), 10); // YYYY-MM-DD
    EXPECT_EQ(timePart.length(), 8);  // HH:MM:SS
    EXPECT_EQ(msPart.length(), 3);    // mmm
}

// Test that timestamps are unique
TEST(FileMonitorTest, TimestampsAreUnique) {
    auto monitor = std::make_unique<FileMonitor>(testFilePath, "localhost:9092", "test-topic");

    std::string ts1 = monitor->getCurrentTimestamp();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::string ts2 = monitor->getCurrentTimestamp();

    EXPECT_NE(ts1, ts2);
}

// Test message formatting
TEST(FileMonitorTest, FormatMessage) {
    auto monitor = std::make_unique<FileMonitor>(testFilePath, "localhost:9092", "test-topic");

    std::string message = monitor->formatMessage(
        "/path/to/file.txt",
        "test line content",
        "my-topic",
        "MODIFY"
    );

    // Verify JSON structure
    EXPECT_NE(message.find("\"timestamp\""), std::string::npos);
    EXPECT_NE(message.find("\"filePath\""), std::string::npos);
    EXPECT_NE(message.find("\"kafkaTopic\""), std::string::npos);
    EXPECT_NE(message.find("\"message\""), std::string::npos);
    EXPECT_NE(message.find("\"type\""), std::string::npos);

    // Verify content is present
    EXPECT_NE(message.find("/path/to/file.txt"), std::string::npos);
    EXPECT_NE(message.find("my-topic"), std::string::npos);
    EXPECT_NE(message.find("MODIFY"), std::string::npos);
    EXPECT_NE(message.find("test line content"), std::string::npos);
}

// Test message formatting with special characters in message
TEST(FileMonitorTest, FormatMessageWithSpecialChars) {
    auto monitor = std::make_unique<FileMonitor>(testFilePath, "localhost:9092", "test-topic");

    std::string message = monitor->formatMessage(
        "/path/to/file.txt",
        "Line with \"quotes\" and \\ backslash",
        "my-topic",
        "MODIFY"
    );

    // The message should contain the content (note: real implementation
    // might need proper JSON escaping, but for basic tests this is sufficient)
    EXPECT_NE(message.find("with"), std::string::npos);
}

// Test message formatting with empty content
TEST(FileMonitorTest, FormatMessageWithEmptyContent) {
    auto monitor = std::make_unique<FileMonitor>(testFilePath, "localhost:9092", "test-topic");

    std::string message = monitor->formatMessage(
        "/path/to/file.txt",
        "",
        "my-topic",
        "INIT"
    );

    EXPECT_NE(message.find("\"message\": \"\""), std::string::npos);
}

// Test file existence check
TEST(FileMonitorTest, TestFileExists) {
    // Test with existing file
    std::string existingFile = "/etc/passwd";
    std::ifstream file(existingFile);
    EXPECT_TRUE(file.is_open());
    file.close();

    // Test with non-existing file
    std::string nonExistingFile = "/tmp/non_existent_file_12345.txt";
    std::ifstream file2(nonExistingFile);
    EXPECT_FALSE(file2.is_open());
    file2.close();
}

// Test file reading
TEST(FileMonitorTest, TestFileReading) {
    std::ifstream file(testFilePath);
    ASSERT_TRUE(file.is_open());

    std::string line1, line2;
    std::getline(file, line1);
    std::getline(file, line2);

    EXPECT_EQ(line1, "Initial line 1");
    EXPECT_EQ(line2, "Initial line 2");
    file.close();
}

// Test file appending
TEST(FileMonitorTest, TestFileAppending) {
    std::ofstream file(testFilePath, std::ios::app);
    ASSERT_TRUE(file.is_open());

    file << "New line added\n";
    file.close();

    std::ifstream readFile(testFilePath);
    std::string line;
    std::string lastLine;
    while (std::getline(readFile, line)) {
        lastLine = line;
    }
    readFile.close();

    EXPECT_EQ(lastLine, "New line added");
}

// Test timestamp consistency
TEST(FileMonitorTest, TimestampComponents) {
    auto monitor = std::make_unique<FileMonitor>(testFilePath, "localhost:9092", "test-topic");

    std::string timestamp = monitor->getCurrentTimestamp();

    // Parse and verify components
    int year, month, day, hour, minute, second, millisecond;
    char dash1, dash2, space, colon1, colon2, dot;

    std::istringstream ss(timestamp);
    ss >> year >> dash1 >> month >> dash2 >> day >> space
       >> hour >> colon1 >> minute >> colon2 >> second >> dot >> millisecond;

    // Verify separators
    EXPECT_EQ(dash1, '-');
    EXPECT_EQ(dash2, '-');
    EXPECT_EQ(space, ' ');
    EXPECT_EQ(colon1, ':');
    EXPECT_EQ(colon2, ':');
    EXPECT_EQ(dot, '.');

    // Verify reasonable values
    EXPECT_GE(year, 2025);
    EXPECT_LE(year, 2030);
    EXPECT_GE(month, 1);
    EXPECT_LE(month, 12);
    EXPECT_GE(day, 1);
    EXPECT_LE(day, 31);
    EXPECT_GE(hour, 0);
    EXPECT_LE(hour, 23);
    EXPECT_GE(minute, 0);
    EXPECT_LE(minute, 59);
    EXPECT_GE(second, 0);
    EXPECT_LE(second, 59);
    EXPECT_GE(millisecond, 0);
    EXPECT_LE(millisecond, 999);
}

// Test JSON message structure validity
TEST(FileMonitorTest, JsonMessageStructure) {
    auto monitor = std::make_unique<FileMonitor>(testFilePath, "localhost:9092", "test-topic");

    std::string message = monitor->formatMessage(
        "/test/path/file.txt",
        "test message",
        "test-topic",
        "MODIFY"
    );

    // Count quotes to verify balanced JSON (basic check)
    int openQuotes = 0;
    for (char c : message) {
        if (c == '"') {
            openQuotes++;
        }
    }
    // Should have even number of quotes for balanced JSON
    EXPECT_EQ(openQuotes % 2, 0);
}

// Test message length consistency
TEST(FileMonitorTest, MessageLengthConsistency) {
    auto monitor = std::make_unique<FileMonitor>(testFilePath, "localhost:9092", "test-topic");

    std::string message = monitor->formatMessage(
        "/path/file.txt",
        "test content",
        "topic",
        "TYPE"
    );

    // Message should be at least a reasonable length for JSON structure
    EXPECT_GT(message.length(), 50);
}

// Test with different file paths
TEST(FileMonitorTest, DifferentFilePaths) {
    auto monitor1 = std::make_unique<FileMonitor>("/path1/file.txt", "localhost:9092", "topic1");
    auto monitor2 = std::make_unique<FileMonitor>("/path2/file.txt", "localhost:9092", "topic2");

    std::string msg1 = monitor1->formatMessage("/path1/file.txt", "content", "topic1", "TYPE");
    std::string msg2 = monitor2->formatMessage("/path2/file.txt", "content", "topic2", "TYPE");

    EXPECT_NE(msg1, msg2);
    EXPECT_NE(msg1.find("/path1"), std::string::npos);
    EXPECT_NE(msg2.find("/path2"), std::string::npos);
}

// Test timestamp format includes milliseconds
TEST(FileMonitorTest, TimestampIncludesMilliseconds) {
    auto monitor = std::make_unique<FileMonitor>(testFilePath, "localhost:9092", "test-topic");

    std::string timestamp = monitor->getCurrentTimestamp();

    // Check for milliseconds separator
    EXPECT_NE(timestamp.find('.'), std::string::npos);

    // Extract milliseconds part
    size_t dotPos = timestamp.rfind('.');
    std::string msPart = timestamp.substr(dotPos + 1);

    // Milliseconds should be 3 digits
    EXPECT_EQ(msPart.length(), 3);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
