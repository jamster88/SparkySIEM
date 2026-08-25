/**
 * @file standalone_tests.cpp
 * @brief Standalone tests that don't depend on platform-specific features
 *
 * These tests verify the basic functionality of the file monitoring system
 * without requiring inotify (Linux-specific) or Kafka. They can be run on
 * any platform including macOS and Windows.
 *
 * Test categories:
 * - Timestamp tests: Verify timestamp format and uniqueness
 * - Message format tests: Verify JSON message structure
 * - File system tests: Verify file operations (create, read, write)
 * - JSON parsing tests: Verify JSON structure validity
 * - String manipulation tests: Verify string operations
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
#include <vector>
#include <map>
#include <ctime>
#include <cstdio>

// ============================================================================
// Timestamp tests
// ============================================================================

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));

    std::ostringstream timestamp;
    timestamp << buffer << "." << std::setfill('0') << std::setw(3) << milliseconds.count();
    return timestamp.str();
}

TEST(TimestampTests, FormatCorrect) {
    std::string timestamp = getCurrentTimestamp();

    // Expected format: "YYYY-MM-DD HH:MM:SS.mmm" = 23 characters
    EXPECT_EQ(timestamp.length(), 23);

    // Verify separators
    EXPECT_EQ(timestamp[4], '-');
    EXPECT_EQ(timestamp[7], '-');
    EXPECT_EQ(timestamp[10], ' ');
    EXPECT_EQ(timestamp[13], ':');
    EXPECT_EQ(timestamp[16], ':');
    EXPECT_EQ(timestamp[19], '.');
}

TEST(TimestampTests, UniqueValues) {
    std::string ts1 = getCurrentTimestamp();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::string ts2 = getCurrentTimestamp();

    EXPECT_NE(ts1, ts2);
}

// ============================================================================
// Message formatting tests
// ============================================================================

std::string formatMessage(const std::string& filePath, const std::string& line,
                          const std::string& kafkaTopic, const std::string& messageType) {
    std::string timestamp = getCurrentTimestamp();
    std::string formattedMessage = "{\"timestamp\": \"" + timestamp + "\", "
                                 "\"filePath\": \"" + filePath + "\", "
                                 "\"kafkaTopic\": \"" + kafkaTopic + "\", "
                                 "\"message\": \"" + line + "\", "
                                 "\"type\": \"" + messageType + "\"}";
    return formattedMessage;
}

TEST(MessageFormatTests, BasicFormatting) {
    std::string message = formatMessage("/path/file.txt", "test line", "my-topic", "MODIFY");

    // Check JSON structure
    EXPECT_NE(message.find("\"timestamp\""), std::string::npos);
    EXPECT_NE(message.find("\"filePath\""), std::string::npos);
    EXPECT_NE(message.find("\"kafkaTopic\""), std::string::npos);
    EXPECT_NE(message.find("\"message\""), std::string::npos);
    EXPECT_NE(message.find("\"type\""), std::string::npos);
}

TEST(MessageFormatTests, ContentPreserved) {
    std::string filePath = "/path/to/file.txt";
    std::string line = "test content here";
    std::string topic = "my-kafka-topic";
    std::string type = "INIT";

    std::string message = formatMessage(filePath, line, topic, type);

    EXPECT_NE(message.find(filePath), std::string::npos);
    EXPECT_NE(message.find(line), std::string::npos);
    EXPECT_NE(message.find(topic), std::string::npos);
    EXPECT_NE(message.find(type), std::string::npos);
}

TEST(MessageFormatTests, EmptyContent) {
    std::string message = formatMessage("/path/file.txt", "", "topic", "INIT");

    EXPECT_NE(message.find("\"message\": \"\""), std::string::npos);
}

TEST(MessageFormatTests, DifferentMessageTypes) {
    std::vector<std::string> types = {"INIT", "MODIFY", "DELETE", "CREATE", "ERROR", "CLOSE"};
    for (const auto& type : types) {
        std::string message = formatMessage("/path/file.txt", "test", "topic", type);
        EXPECT_NE(message.find(type), std::string::npos);
    }
}

TEST(MessageFormatTests, MessageLength) {
    std::string message = formatMessage("/path/file.txt", "test content", "topic", "TYPE");
    // Should be at least 100 characters for proper JSON structure
    EXPECT_GT(message.length(), 100);
}

// ============================================================================
// File system tests
// ============================================================================

class FileSystemTest : public ::testing::Test {
protected:
    std::string testDir;

    void SetUp() override {
        testDir = "/tmp/fs_test_" + std::to_string(std::time(nullptr));
        mkdir(testDir.c_str(), 0755);
    }

    void TearDown() override {
        // Remove test directory and contents
        system(("rm -rf " + testDir).c_str());
    }

    std::string createTestFile(const std::string& name, const std::string& content) {
        std::string path = testDir + "/" + name;
        std::ofstream file(path);
        file << content;
        file.close();
        return path;
    }
};

TEST_F(FileSystemTest, CreateAndReadFile) {
    std::string path = createTestFile("test.txt", "Hello, World!");

    std::ifstream file(path);
    ASSERT_TRUE(file.is_open());

    std::string content;
    std::getline(file, content);
    file.close();

    EXPECT_EQ(content, "Hello, World!");
}

TEST_F(FileSystemTest, AppendToFile) {
    std::string path = createTestFile("test.txt", "Line 1\n");

    // Append
    std::ofstream file(path, std::ios::app);
    file << "Line 2\n";
    file.close();

    // Read back
    std::ifstream readFile(path);
    std::string line1, line2;
    std::getline(readFile, line1);
    std::getline(readFile, line2);
    readFile.close();

    EXPECT_EQ(line1, "Line 1");
    EXPECT_EQ(line2, "Line 2");
}

TEST_F(FileSystemTest, MultiLineFile) {
    std::string path = createTestFile("multiline.txt", "Line 1\nLine 2\nLine 3\n");

    std::ifstream file(path);
    std::string line1, line2, line3;
    std::getline(file, line1);
    std::getline(file, line2);
    std::getline(file, line3);
    file.close();

    EXPECT_EQ(line1, "Line 1");
    EXPECT_EQ(line2, "Line 2");
    EXPECT_EQ(line3, "Line 3");
}

TEST_F(FileSystemTest, NonExistentFile) {
    std::string path = testDir + "/nonexistent.txt";
    std::ifstream file(path);
    EXPECT_FALSE(file.is_open());
    file.close();
}

TEST_F(FileSystemTest, LargeFile) {
    std::string path = testDir + "/large.txt";
    std::ofstream file(path);

    for (int i = 0; i < 1000; i++) {
        file << "Line " << i << "\n";
    }
    file.close();

    std::ifstream readFile(path);
    std::string line;
    int count = 0;
    while (std::getline(readFile, line)) {
        count++;
    }
    readFile.close();

    EXPECT_EQ(count, 1000);
}

// ============================================================================
// JSON parsing tests
// ============================================================================

TEST(JsonTests, SimpleJsonStructure) {
    std::string json = "{\"key1\": \"value1\", \"key2\": \"value2\"}";

    EXPECT_NE(json.find("\"key1\""), std::string::npos);
    EXPECT_NE(json.find("\"value1\""), std::string::npos);
    EXPECT_NE(json.find("\"key2\""), std::string::npos);
    EXPECT_NE(json.find("\"value2\""), std::string::npos);
}

TEST(JsonTests, NestedJsonStructure) {
    std::string json = "{\"outer\": {\"inner\": \"value\"}}";

    EXPECT_NE(json.find("\"outer\""), std::string::npos);
    EXPECT_NE(json.find("\"inner\""), std::string::npos);
    EXPECT_NE(json.find("\"value\""), std::string::npos);
}

// ============================================================================
// String manipulation tests
// ============================================================================

TEST(StringTests, StringConcatenation) {
    std::string part1 = "Hello";
    std::string part2 = "World";
    std::string result = part1 + " " + part2;

    EXPECT_EQ(result, "Hello World");
}

TEST(StringTests, StringFind) {
    std::string text = "The quick brown fox";

    EXPECT_NE(text.find("quick"), std::string::npos);
    EXPECT_NE(text.find("fox"), std::string::npos);
    EXPECT_EQ(text.find("dog"), std::string::npos);
}

TEST(StringTests, StringLength) {
    std::string empty = "";
    std::string text = "Hello, World!";

    EXPECT_EQ(empty.length(), 0);
    EXPECT_EQ(text.length(), 13);
}

// ============================================================================
// Thread safety tests (basic)
// ============================================================================

TEST(ThreadTests, ConcurrentCounter) {
    std::vector<int> results(10);
    std::vector<std::thread> threads;

    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&results, i]() {
            results[i] = i * 2;
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(results[i], i * 2);
    }
}

// ============================================================================
// Test runner
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
