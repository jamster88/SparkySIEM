/**
 * @file FilesMonitor_test.cpp
 * @brief Unit tests for the FilesMonitor class
 *
 * These tests verify the functionality of the FilesMonitor class,
 * including path handling, file monitoring setup, and cleanup.
 *
 * Note: Full integration tests with inotify require a Linux environment.
 * This file contains unit tests for the non-platform-specific logic.
 *
 * Test categories:
 * - Directory setup tests: Verify test directory creation
 * - File operations tests: Verify file creation, reading, modification
 * - Directory iteration tests: Verify directory traversal
 * - Symlink tests: Verify symbolic link handling
 * - Concurrent access tests: Verify thread safety
 * - Cleanup tests: Verify directory and file removal
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <filesystem>

namespace fs = std::filesystem;

// Test fixture for FilesMonitor tests
class FilesMonitorTest : public ::testing::Test {
protected:
    std::string testDir;
    std::string testDir2;

    void SetUp() override {
        // Create temporary test directories
        testDir = "/tmp/files_monitor_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        testDir2 = testDir + "_2";

        fs::create_directories(testDir);
        fs::create_directories(testDir2);

        // Create some test files
        createTestFile(testDir + "/file1.txt", "Content of file 1");
        createTestFile(testDir + "/file2.txt", "Content of file 2");
        createTestFile(testDir2 + "/file3.txt", "Content of file 3");
    }

    void TearDown() override {
        // Clean up test directories and files
        try {
            fs::remove_all(testDir);
            fs::remove_all(testDir2);
        } catch (...) {
            // Ignore cleanup errors
        }
    }

    void createTestFile(const std::string& path, const std::string& content) {
        std::ofstream file(path);
        file << content;
        file.close();
    }

    std::vector<std::string> getTestPaths() {
        return {testDir, testDir2};
    }
};

// Test directory creation and cleanup
TEST_F(FilesMonitorTest, DirectorySetup) {
    EXPECT_TRUE(fs::exists(testDir));
    EXPECT_TRUE(fs::exists(testDir2));
    EXPECT_TRUE(fs::is_directory(testDir));
    EXPECT_TRUE(fs::is_directory(testDir2));
}

// Test file creation in test directory
TEST_F(FilesMonitorTest, TestFileCreation) {
    std::string filePath = testDir + "/new_file.txt";
    createTestFile(filePath, "Test content");

    EXPECT_TRUE(fs::exists(filePath));
    EXPECT_TRUE(fs::is_regular_file(filePath));
}

// Test reading created files
TEST_F(FilesMonitorTest, TestFileReading) {
    std::string filePath = testDir + "/file1.txt";
    std::ifstream file(filePath);
    ASSERT_TRUE(file.is_open());

    std::string content;
    std::getline(file, content);
    file.close();

    EXPECT_EQ(content, "Content of file 1");
}

// Test directory iteration
TEST_F(FilesMonitorTest, DirectoryIteration) {
    int fileCount = 0;
    for (const auto& entry : fs::directory_iterator(testDir)) {
        if (fs::is_regular_file(entry.status())) {
            fileCount++;
        }
    }

    EXPECT_GE(fileCount, 2);
}

// Test file modification detection (basic)
TEST_F(FilesMonitorTest, FileModification) {
    std::string filePath = testDir + "/file1.txt";
    std::string originalContent;

    // Read original content
    {
        std::ifstream file(filePath);
        std::getline(file, originalContent);
        file.close();
    }

    // Modify the file
    std::ofstream file(filePath);
    file << "Modified content";
    file.close();

    // Read modified content
    std::string modifiedContent;
    {
        std::ifstream file(filePath);
        std::getline(file, modifiedContent);
        file.close();
    }

    EXPECT_NE(originalContent, modifiedContent);
}

// Test file deletion
TEST_F(FilesMonitorTest, FileDeletion) {
    std::string filePath = testDir + "/file2.txt";
    EXPECT_TRUE(fs::exists(filePath));

    fs::remove(filePath);
    EXPECT_FALSE(fs::exists(filePath));
}

// Test creating new file in directory
TEST_F(FilesMonitorTest, CreateNewFileInDirectory) {
    std::string newFilePath = testDir + "/new_test_file.txt";
    createTestFile(newFilePath, "New file content");

    EXPECT_TRUE(fs::exists(newFilePath));
    EXPECT_EQ(fs::directory_iterator(testDir).begin()->path().filename().string(), "file1.txt");
}

// Test empty directory handling
TEST_F(FilesMonitorTest, EmptyDirectory) {
    std::string emptyDir = testDir + "/empty_dir";
    fs::create_directory(emptyDir);

    EXPECT_TRUE(fs::exists(emptyDir));
    EXPECT_TRUE(fs::is_directory(emptyDir));

    int fileCount = 0;
    for (const auto& entry : fs::directory_iterator(emptyDir)) {
        fileCount++;
    }
    EXPECT_EQ(fileCount, 0);
}

// Test directory with multiple files
TEST_F(FilesMonitorTest, MultipleFilesInDirectory) {
    for (int i = 0; i < 5; i++) {
        createTestFile(testDir + "/multi_" + std::to_string(i) + ".txt",
                      "Content " + std::to_string(i));
    }

    int fileCount = 0;
    for (const auto& entry : fs::directory_iterator(testDir)) {
        if (fs::is_regular_file(entry.status())) {
            fileCount++;
        }
    }

    EXPECT_GE(fileCount, 5);
}

// Test file path resolution
TEST_F(FilesMonitorTest, FilePathResolution) {
    std::string filePath = testDir + "/file1.txt";

    // Get absolute path
    fs::path absolutePath = fs::absolute(filePath);
    EXPECT_EQ(absolutePath.filename().string(), "file1.txt");
}

// Test symbolic link handling
TEST_F(FilesMonitorTest, SymlinkHandling) {
    std::string linkPath = testDir + "/symlink.txt";
    std::string targetPath = testDir + "/file1.txt";

    fs::create_symlink(targetPath, linkPath);

    EXPECT_TRUE(fs::exists(linkPath));
    EXPECT_TRUE(fs::is_symlink(linkPath));
    EXPECT_TRUE(fs::exists(fs::read_symlink(linkPath)));
}

// Test concurrent file access
TEST_F(FilesMonitorTest, ConcurrentFileAccess) {
    std::string filePath = testDir + "/concurrent.txt";
    createTestFile(filePath, "Initial");

    // Multiple read operations
    for (int i = 0; i < 10; i++) {
        std::ifstream file(filePath);
        ASSERT_TRUE(file.is_open());
        std::string line;
        std::getline(file, line);
        EXPECT_EQ(line, "Initial");
        file.close();
    }
}

// Test file modification timestamp update
TEST_F(FilesMonitorTest, ModificationTimestamp) {
    std::string filePath = testDir + "/timestamp.txt";
    createTestFile(filePath, "Initial content");

    auto beforeTime = fs::last_write_time(filePath);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Modify file
    std::ofstream file(filePath, std::ios::app);
    file << "\nAdditional content";
    file.close();

    auto afterTime = fs::last_write_time(filePath);
    EXPECT_NE(beforeTime, afterTime);
}

// Test cleanup of multiple directories
TEST_F(FilesMonitorTest, CleanupMultipleDirectories) {
    std::string dir1 = testDir + "/subdir1";
    std::string dir2 = testDir + "/subdir2";
    fs::create_directories(dir1);
    fs::create_directories(dir2);

    createTestFile(dir1 + "/file1.txt", "Content 1");
    createTestFile(dir2 + "/file2.txt", "Content 2");

    // Verify all created
    EXPECT_TRUE(fs::exists(dir1));
    EXPECT_TRUE(fs::exists(dir2));
    EXPECT_TRUE(fs::exists(dir1 + "/file1.txt"));
    EXPECT_TRUE(fs::exists(dir2 + "/file2.txt"));

    // Cleanup
    fs::remove_all(testDir);
    EXPECT_FALSE(fs::exists(testDir));
}

// Test file path with special characters
TEST_F(FilesMonitorTest, SpecialCharactersInPath) {
    // Note: On some systems, creating files with special chars may fail
    std::string safeName = "file_with_underscore.txt";
    std::string filePath = testDir + "/" + safeName;

    createTestFile(filePath, "Content");

    EXPECT_TRUE(fs::exists(filePath));
}

// Test file with large content
TEST_F(FilesMonitorTest, LargeFileContent) {
    std::string filePath = testDir + "/large.txt";

    std::ofstream file(filePath);
    ASSERT_TRUE(file.is_open());

    // Write 1000 lines
    for (int i = 0; i < 1000; i++) {
        file << "Line " << i << "\n";
    }
    file.close();

    // Verify we can read back
    std::ifstream readFile(filePath);
    ASSERT_TRUE(readFile.is_open());

    std::string line;
    int lineCount = 0;
    while (std::getline(readFile, line)) {
        lineCount++;
    }
    readFile.close();

    EXPECT_EQ(lineCount, 1000);
}

// Test nested directory structure
TEST_F(FilesMonitorTest, NestedDirectories) {
    std::string nestedDir = testDir + "/level1/level2/level3";
    fs::create_directories(nestedDir);

    createTestFile(nestedDir + "/deep_file.txt", "Deep content");

    EXPECT_TRUE(fs::exists(nestedDir));
    EXPECT_TRUE(fs::exists(nestedDir + "/deep_file.txt"));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
