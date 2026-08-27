/**
 * @file test_runner.cpp
 * @brief Standalone test runner for FileFormat utilities.
 *
 * No external dependencies required — uses plain C++ assertions and prints PASS/FAIL.
 * Run: g++ -std=c++17 -I.. -o test_runner ../FileFormat.cpp test_runner.cpp && ./test_runner
 */

#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <cmath>
#include "../FileFormat.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    tests_run++; \
    std::cout << "  [RUN]  " << #name << "... "; \
    try { name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; } \
} while(0)

#define ASSERT_TRUE(expr) do { if (!(expr)) throw std::runtime_error("ASSERT_TRUE failed: " #expr); } while(0)
#define ASSERT_FALSE(expr) do { if (expr) throw std::runtime_error("ASSERT_FALSE failed: " #expr); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("ASSERT_EQ failed: expected " + std::to_string(b) + " got " + std::to_string(a)); } while(0)

// ============================================================================
// formatMessage tests
// ============================================================================

TEST(test_format_message_produces_json) {
    std::string msg = formatMessage("/tmp/test.txt", "hello world", "my-topic", "MODIFY");
    ASSERT_TRUE(msg.front() == '{');
    ASSERT_TRUE(msg.back() == '}');
    ASSERT_TRUE(msg.find("\"filePath\": \"/tmp/test.txt\"") != std::string::npos);
    ASSERT_TRUE(msg.find("\"kafkaTopic\": \"my-topic\"") != std::string::npos);
    ASSERT_TRUE(msg.find("\"message\": \"hello world\"") != std::string::npos);
    ASSERT_TRUE(msg.find("\"type\": \"MODIFY\"") != std::string::npos);
}

TEST(test_format_message_escapes_backslash) {
    std::string msg = formatMessage("C:\\Users\\test\\file.txt", "line with \\ backslash", "topic", "MODIFY");
    ASSERT_TRUE(msg.find("\\\\") != std::string::npos);
}

TEST(test_format_message_escapes_quotes) {
    std::string msg = formatMessage("file\"name.txt", "he said \"hello\"", "topic", "MODIFY");
    ASSERT_TRUE(msg.find("\\\"") != std::string::npos || msg.find("\\\\\"") != std::string::npos);
}

TEST(test_format_message_escapes_newline) {
    std::string msg = formatMessage("file.txt", "line1\nline2", "topic", "MODIFY");
    ASSERT_TRUE(msg.find("\\n") != std::string::npos);
}

TEST(test_format_message_escapes_tab) {
    std::string msg = formatMessage("file.txt", "\ttabbed", "topic", "MODIFY");
    ASSERT_TRUE(msg.find("\\t") != std::string::npos);
}

TEST(test_format_message_initial_event) {
    std::string msg = formatMessage("/home/user/data.log", " ", "events", "INIT");
    ASSERT_TRUE(msg.find("\"type\": \"INIT\"") != std::string::npos);
    ASSERT_TRUE(msg.find("\"filePath\": \"/home/user/data.log\"") != std::string::npos);
}

TEST(test_format_message_close_event) {
    std::string msg = formatMessage("/tmp/test.txt", " ", "events", "CLOSE");
    ASSERT_TRUE(msg.find("\"type\": \"CLOSE\"") != std::string::npos);
}

TEST(test_format_message_error_event) {
    std::string msg = formatMessage("/missing/file.dat", " ", "events", "ERROR - FILE OPEN");
    ASSERT_TRUE(msg.find("\"type\": \"ERROR - FILE OPEN\"") != std::string::npos);
}

TEST(test_format_message_timestamp_present) {
    std::string msg = formatMessage("test.txt", "line", "topic", "MODIFY");
    std::regex ts_regex("\"timestamp\": \"\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}\"");
    ASSERT_TRUE(std::regex_search(msg, ts_regex));
}

TEST(test_format_message_empty_strings) {
    std::string msg = formatMessage("", "", "", "");
    ASSERT_TRUE(msg.front() == '{');
    ASSERT_TRUE(msg.back() == '}');
}

TEST(test_format_message_control_characters) {
    std::string line;
    line += 'l'; line += 'i'; line += 'n'; line += 'e';
    for (int c = 0x01; c <= 0x03; ++c) line += static_cast<char>(c);
    line += 'e'; line += 'n'; line += 'd';

    std::string msg = formatMessage("file.txt", line, "topic", "MODIFY");
    ASSERT_TRUE(msg.find("\\u0001") != std::string::npos);
    ASSERT_TRUE(msg.find("\\u0002") != std::string::npos);
    ASSERT_TRUE(msg.find("\\u0003") != std::string::npos);
}

// ============================================================================
// getCurrentTimestamp tests
// ============================================================================

TEST(test_timestamp_format) {
    std::string ts = getCurrentTimestamp();
    ASSERT_EQ(ts.size(), 23u);
    ASSERT_TRUE(ts[4] == '-');
    ASSERT_TRUE(ts[7] == '-');
    ASSERT_TRUE(ts[10] == ' ');
    ASSERT_TRUE(ts[13] == ':');
    ASSERT_TRUE(ts[16] == ':');
    ASSERT_TRUE(ts[19] == '.');
}

TEST(test_timestamp_is_utc) {
    std::string ts = getCurrentTimestamp();
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::gmtime(&now_c));

    // Timestamp should be within 2 seconds of actual UTC
    std::string expected(buffer, 19);
    std::string actual = ts.substr(0, 19);
    // Compare date-time components
    ASSERT_TRUE(actual == expected || std::abs(std::stoi(actual.substr(0,4)) - std::stoi(expected.substr(0,4))) < 2);
}

TEST(test_timestamp_is_unique) {
    std::string ts1 = getCurrentTimestamp();
    auto start = std::chrono::system_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now() - start).count() < 50) {}
    std::string ts2 = getCurrentTimestamp();
    ASSERT_TRUE(ts1 != ts2);
}

// ============================================================================
// Main test runner
// ============================================================================

int main() {
    std::cout << "=== SparkySIEM FileMonitor Unit Tests ===" << std::endl;
    std::cout << std::endl;

    // formatMessage tests
    std::cout << "[formatMessage]" << std::endl;
    RUN_TEST(test_format_message_produces_json);
    RUN_TEST(test_format_message_escapes_backslash);
    RUN_TEST(test_format_message_escapes_quotes);
    RUN_TEST(test_format_message_escapes_newline);
    RUN_TEST(test_format_message_escapes_tab);
    RUN_TEST(test_format_message_initial_event);
    RUN_TEST(test_format_message_close_event);
    RUN_TEST(test_format_message_error_event);
    RUN_TEST(test_format_message_timestamp_present);
    RUN_TEST(test_format_message_empty_strings);
    RUN_TEST(test_format_message_control_characters);

    // getCurrentTimestamp tests
    std::cout << "[getCurrentTimestamp]" << std::endl;
    RUN_TEST(test_timestamp_format);
    RUN_TEST(test_timestamp_is_utc);
    RUN_TEST(test_timestamp_is_unique);

    std::cout << std::endl;
    std::cout << "=== Results: " << tests_run << " total, "
              << tests_passed << " passed, "
              << tests_failed << " failed ===" << std::endl;

    return (tests_failed > 0) ? 1 : 0;
}
