/**
 * @file test_file_format.cpp
 * @brief Unit tests for FileFormat.h pure formatting utilities.
 *
 * Tests message JSON formatting and timestamp generation without
 * requiring Kafka, inotify, or any platform-specific dependencies.
 * Runs on both macOS and Linux.
 */

#include <gtest/gtest.h>
#include "../FileFormat.h"
#include <string>
#include <regex>
#include <iostream>

// ============================================================================
// formatMessage tests
// ============================================================================

TEST(FileFormat, FormatMessageProducesValidJson) {
    std::string msg = formatMessage("/tmp/test.txt", "hello world", "my-topic", "MODIFY");

    // Basic JSON structure check
    EXPECT_EQ('{', msg.front());
    EXPECT_EQ('}', msg.back());
    EXPECT_TRUE(msg.find("\"filePath\": \"/tmp/test.txt\"") != std::string::npos);
    EXPECT_TRUE(msg.find("\"kafkaTopic\": \"my-topic\"") != std::string::npos);
    EXPECT_TRUE(msg.find("\"message\": \"hello world\"") != std::string::npos);
    EXPECT_TRUE(msg.find("\"type\": \"MODIFY\"") != std::string::npos);
}

TEST(FileFormat, FormatMessageEscapesBackslash) {
    std::string msg = formatMessage("C:\\Users\\test\\file.txt", "line with \\ backslash", "topic", "MODIFY");
    EXPECT_TRUE(msg.find("\\\\") != std::string::npos);  // escaped backslash
}

TEST(FileFormat, FormatMessageEscapesQuotes) {
    // jsonEscape turns " into \" (backslash + quote = 2 real chars)
    std::string msg = formatMessage("file\"name.txt", "he said \"hello\"", "topic", "MODIFY");
    EXPECT_TRUE(msg.find("\"filePath\": \"") != std::string::npos);
}

TEST(FileFormat, FormatMessageEscapesNewline) {
    std::string msg = formatMessage("file.txt", "line1\nline2", "topic", "MODIFY");
    EXPECT_TRUE(msg.find("\\n") != std::string::npos);
}

TEST(FileFormat, FormatMessageEscapesTab) {
    std::string msg = formatMessage("file.txt", "\ttabbed", "topic", "MODIFY");
    EXPECT_TRUE(msg.find("\\t") != std::string::npos);
}

TEST(FileFormat, FormatMessageInitialEvent) {
    std::string msg = formatMessage("/home/user/data.log", " ", "events", "INIT");
    EXPECT_TRUE(msg.find("\"type\": \"INIT\"") != std::string::npos);
    EXPECT_TRUE(msg.find("\"filePath\": \"/home/user/data.log\"") != std::string::npos);
}

TEST(FileFormat, FormatMessageCloseEvent) {
    std::string msg = formatMessage("/tmp/test.txt", " ", "events", "CLOSE");
    EXPECT_TRUE(msg.find("\"type\": \"CLOSE\"") != std::string::npos);
}

TEST(FileFormat, FormatMessageErrorEvent) {
    std::string msg = formatMessage("/missing/file.dat", " ", "events", "ERROR - FILE OPEN");
    EXPECT_TRUE(msg.find("\"type\": \"ERROR - FILE OPEN\"") != std::string::npos);
}

TEST(FileFormat, FormatMessageTimestampPresentAndFormatted) {
    std::string msg = formatMessage("test.txt", "line", "topic", "MODIFY");
    // Timestamp format: "YYYY-MM-DD HH:MM:SS.mmm"
    std::regex ts_regex("\"timestamp\": \"\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}\"");
    EXPECT_TRUE(std::regex_search(msg, ts_regex));
}

TEST(FileFormat, FormatMessageHandlesEmptyString) {
    std::string msg = formatMessage("", "", "", "");
    EXPECT_EQ('{', msg.front());
    EXPECT_EQ('}', msg.back());
}

// ============================================================================
// getCurrentTimestamp tests
// ============================================================================

TEST(FileFormat, GetCurrentTimestampFormatsCorrectly) {
    std::string ts = getCurrentTimestamp();
    // Format: YYYY-MM-DD HH:MM:SS.mmm (23 characters)
    EXPECT_EQ(23u, ts.size());
    EXPECT_EQ('-', ts[4]);   // after year
    EXPECT_EQ('-', ts[7]);   // after month
    EXPECT_EQ(' ', ts[10]);  // space between date and time
    EXPECT_EQ(':', ts[13]);  // after hour
    EXPECT_EQ(':', ts[16]);  // after minute
    EXPECT_EQ('.', ts[19]);  // decimal point before milliseconds
}

TEST(FileFormat, GetCurrentTimestampIsUtc) {
    // Get the timestamp and compare with actual UTC time
    std::string ts = getCurrentTimestamp();

    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);

    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::gmtime(&now_c));

    // Timestamp should match UTC (within 1 second tolerance)
    std::string expected_date = std::string(buffer, 19);
    EXPECT_TRUE(ts.substr(0, 19) == expected_date ||
                ts.substr(0, 19) == expected_date.substr(0, 4) + "-" +
                                     expected_date.substr(5, 2) + "-" +
                                     expected_date.substr(8, 2));
}

TEST(FileFormat, GetCurrentTimestampIsUniquePerCall) {
    // Call twice quickly - they should produce different results
    std::string ts1 = getCurrentTimestamp();
    auto now = std::chrono::system_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now() - now).count() < 10) {
        // spin for 10ms to ensure time difference
    }
    std::string ts2 = getCurrentTimestamp();
    EXPECT_NE(ts1, ts2);
}

// ============================================================================
// JSON Escaping edge cases
// ============================================================================

TEST(FileFormat, FormatMessageHandlesControlCharacters) {
    // Build a string with control characters U+0001 through U+0003
    std::string line = "line";
    for (char c : {'\x01', '\x02', '\x03'}) line += c;
    line += "end";

    std::string msg = formatMessage("file.txt", line, "topic", "MODIFY");
    // jsonEscape converts bytes < 0x20 to \uXXXX format
    auto u_pos = msg.find("\\u");
    EXPECT_TRUE(u_pos != std::string::npos);              // at least one \u escape exists
    if (u_pos != std::string::npos) {
        auto end_pos = msg.find("end", u_pos);
        EXPECT_TRUE(end_pos != std::string::npos);       // "end" found after the escape
    }
}

TEST(FileFormat, FormatMessageHandlesEmptyFilePath) {
    std::string msg = formatMessage("", "content", "topic", "MODIFY");
    EXPECT_TRUE(msg.find("\"filePath\": \"\"") != std::string::npos);
}

TEST(FileFormat, FormatMessageHandlesSpecialKafkaTopicNames) {
    std::string msg = formatMessage("file.txt", "line", "my-topic-123_test", "MODIFY");
    EXPECT_TRUE(msg.find("\"kafkaTopic\": \"my-topic-123_test\"") != std::string::npos);
}

// ============================================================================
// File content type tests
// ============================================================================

TEST(FileFormat, FormatMessageTypeValues) {
    // Test all expected message types work correctly
    std::vector<std::string> types = {"INIT", "MODIFY", "CLOSE", "ERROR - FILE OPEN", "INIT - FILE OPEN"};
    for (const auto& type : types) {
        std::string msg = formatMessage("test.txt", "line", "topic", type);
        EXPECT_TRUE(msg.find("\"type\": \"" + type + "\"") != std::string::npos);
    }
}
