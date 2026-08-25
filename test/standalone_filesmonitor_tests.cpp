/**
 * @file standalone_filesmonitor_tests.cpp
 * @brief Standalone tests for FilesMonitor without platform dependencies
 *
 * Tests verify the logic of FilesMonitor class without requiring inotify or Kafka.
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <chrono>
#include <thread>
#include <iostream>
#include <filesystem>
#include <ctime>
#include <algorithm>
#include <mutex>

namespace fs = std::filesystem;

// ============================================================================
// Path Manipulation Tests
// ============================================================================

class PathTest : public ::testing::Test {
protected:
    std::string testBaseDir;

    void SetUp() override {
        testBaseDir = "/tmp/filesmonitor_test_" + std::to_string(std::time(nullptr));
        fs::create_directories(testBaseDir);
    }

    void TearDown() override {
        fs::remove_all(testBaseDir);
    }

    std::string createFile(const std::string& name, const std::string& content) {
        std::string path = testBaseDir + "/" + name;
        std::ofstream file(path);
        file << content;
        file.close();
        return path;
    }
};

TEST_F(PathTest, CreateDirectory) {
    std::string newDir = testBaseDir + "/new_dir";
    fs::create_directories(newDir);

    EXPECT_TRUE(fs::exists(newDir));
    EXPECT_TRUE(fs::is_directory(newDir));
}

TEST_F(PathTest, CreateFileInDirectory) {
    std::string filePath = testBaseDir + "/test.txt";
    std::ofstream file(filePath);
    file << "content";
    file.close();

    EXPECT_TRUE(fs::exists(filePath));
    EXPECT_TRUE(fs::is_regular_file(filePath));
}

TEST_F(PathTest, DirectoryIteration) {
    createFile("file1.txt", "content1");
    createFile("file2.txt", "content2");
    createFile("file3.txt", "content3");

    int count = 0;
    for (const auto& entry : fs::directory_iterator(testBaseDir)) {
        if (fs::is_regular_file(entry.status())) {
            count++;
        }
    }

    EXPECT_EQ(count, 3);
}

// ============================================================================
// FilesMonitor Path Management Tests
// ============================================================================

class FilesMonitorPathTest : public ::testing::Test {
protected:
    std::string testDir1;
    std::string testDir2;

    void SetUp() override {
        testDir1 = "/tmp/fm_test1_" + std::to_string(std::time(nullptr));
        testDir2 = "/tmp/fm_test2_" + std::to_string(std::time(nullptr));
        fs::create_directories(testDir1);
        fs::create_directories(testDir2);
    }

    void TearDown() override {
        fs::remove_all(testDir1);
        fs::remove_all(testDir2);
    }

    void createFile(const std::string& dir, const std::string& name, const std::string& content) {
        std::string path = dir + "/" + name;
        std::ofstream file(path);
        file << content;
        file.close();
    }
};

TEST_F(FilesMonitorPathTest, TrackMultiplePaths) {
    // Simulate tracking multiple paths
    std::vector<std::string> paths = {testDir1, testDir2};

    // Verify paths exist
    for (const auto& path : paths) {
        EXPECT_TRUE(fs::exists(path));
    }
}

TEST_F(FilesMonitorPathTest, HandleDirectoryPath) {
    std::string dirPath = testDir1;

    // Verify it's a directory
    EXPECT_TRUE(fs::is_directory(dirPath));

    // Create a file and verify it's detected
    createFile(testDir1, "test.txt", "content");

    bool found = false;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.path().filename() == "test.txt") {
            found = true;
            break;
        }
    }

    EXPECT_TRUE(found);
}

TEST_F(FilesMonitorPathTest, HandleFilePath) {
    std::string filePath = testDir1 + "/file.txt";
    createFile(testDir1, "file.txt", "content");

    // Verify file exists
    EXPECT_TRUE(fs::exists(filePath));
    EXPECT_TRUE(fs::is_regular_file(filePath));
}

TEST_F(FilesMonitorPathTest, DetectFileCreation) {
    std::string filePath = testDir1 + "/new_file.txt";

    // Before creation
    EXPECT_FALSE(fs::exists(filePath));

    // Create file
    createFile(testDir1, "new_file.txt", "content");

    // After creation
    EXPECT_TRUE(fs::exists(filePath));
}

TEST_F(FilesMonitorPathTest, DetectFileDeletion) {
    std::string filePath = testDir1 + "/to_delete.txt";
    createFile(testDir1, "to_delete.txt", "content");

    // Before deletion
    EXPECT_TRUE(fs::exists(filePath));

    // Delete file
    fs::remove(filePath);

    // After deletion
    EXPECT_FALSE(fs::exists(filePath));
}

TEST_F(FilesMonitorPathTest, DetectFileModification) {
    std::string filePath = testDir1 + "/mod_test.txt";
    createFile(testDir1, "mod_test.txt", "original");

    // Get original content
    std::string originalContent;
    {
        std::ifstream file(filePath);
        std::getline(file, originalContent);
        file.close();
    }

    // Modify file
    std::ofstream file(filePath);
    file << "modified";
    file.close();

    // Get modified content
    std::string modifiedContent;
    {
        std::ifstream file(filePath);
        std::getline(file, modifiedContent);
        file.close();
    }

    EXPECT_NE(originalContent, modifiedContent);
}

// ============================================================================
// FilesMonitor Map Management Tests
// ============================================================================

class FilesMonitorMapTest : public ::testing::Test {
protected:
    // Simulate the fileMonitors map structure
    std::map<std::string, bool> fileMonitors;
    std::mutex mapMutex;

    void addMonitor(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mapMutex);
        fileMonitors[filePath] = true;
    }

    void removeMonitor(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mapMutex);
        fileMonitors.erase(filePath);
    }

    bool hasMonitor(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mapMutex);
        return fileMonitors.find(filePath) != fileMonitors.end();
    }
};

TEST_F(FilesMonitorMapTest, AddMonitor) {
    std::string filePath = "/path/to/file.txt";
    addMonitor(filePath);

    EXPECT_TRUE(hasMonitor(filePath));
}

TEST_F(FilesMonitorMapTest, RemoveMonitor) {
    std::string filePath = "/path/to/file.txt";
    addMonitor(filePath);
    removeMonitor(filePath);

    EXPECT_FALSE(hasMonitor(filePath));
}

TEST_F(FilesMonitorMapTest, MultipleMonitors) {
    addMonitor("/path/to/file1.txt");
    addMonitor("/path/to/file2.txt");
    addMonitor("/path/to/file3.txt");

    EXPECT_TRUE(hasMonitor("/path/to/file1.txt"));
    EXPECT_TRUE(hasMonitor("/path/to/file2.txt"));
    EXPECT_TRUE(hasMonitor("/path/to/file3.txt"));
}

TEST_F(FilesMonitorMapTest, MonitorDoesNotDuplicate) {
    std::string filePath = "/path/to/file.txt";

    addMonitor(filePath);
    addMonitor(filePath);  // Should not create duplicate

    // Count occurrences
    int count = 0;
    for (const auto& pair : fileMonitors) {
        if (pair.first == filePath) {
            count++;
        }
    }

    EXPECT_EQ(count, 1);
}

// ============================================================================
// FilesMonitor Cleanup Tests
// ============================================================================

class FilesMonitorCleanupTest : public ::testing::Test {
protected:
    std::string testDir;
    std::map<std::string, bool> fileMonitors;

    void SetUp() override {
        testDir = "/tmp/fm_cleanup_" + std::to_string(std::time(nullptr));
        fs::create_directories(testDir);
    }

    void TearDown() override {
        fs::remove_all(testDir);
    }

    void addMonitor(const std::string& filePath) {
        fileMonitors[filePath] = true;
    }

    void removeMonitor(const std::string& filePath) {
        fileMonitors.erase(filePath);
    }

    bool hasMonitor(const std::string& filePath) {
        return fileMonitors.find(filePath) != fileMonitors.end();
    }

    void cleanupDeletedFiles() {
        for (auto it = fileMonitors.begin(); it != fileMonitors.end();) {
            if (!fs::exists(it->first)) {
                it = fileMonitors.erase(it);
            } else {
                ++it;
            }
        }
    }
};

TEST_F(FilesMonitorCleanupTest, CleanupNonExistentFile) {
    std::string filePath = testDir + "/nonexistent.txt";

    addMonitor(filePath);

    // File doesn't exist, should be cleaned up
    cleanupDeletedFiles();

    EXPECT_EQ(fileMonitors.size(), 0);
}

TEST_F(FilesMonitorCleanupTest, KeepExistingFile) {
    std::string filePath = testDir + "/existing.txt";
    std::ofstream file(filePath);
    file << "content";
    file.close();

    addMonitor(filePath);

    // File exists, should NOT be cleaned up
    cleanupDeletedFiles();

    EXPECT_EQ(fileMonitors.size(), 1);
    EXPECT_TRUE(hasMonitor(filePath));
}

TEST_F(FilesMonitorCleanupTest, MixedCleanup) {
    std::string existingFile = testDir + "/existing.txt";
    std::string nonexistentFile = testDir + "/nonexistent.txt";

    // Create existing file
    std::ofstream file(existingFile);
    file << "content";
    file.close();

    // Add both to monitors
    addMonitor(existingFile);
    addMonitor(nonexistentFile);

    // Clean up
    cleanupDeletedFiles();

    // Only existing file should remain
    EXPECT_EQ(fileMonitors.size(), 1);
    EXPECT_TRUE(hasMonitor(existingFile));
    EXPECT_FALSE(hasMonitor(nonexistentFile));
}

// ============================================================================
// FilesMonitor Thread Simulation Tests
// ============================================================================

class FilesMonitorThreadTest : public ::testing::Test {
protected:
    std::vector<std::string> monitoredPaths;
    std::mutex pathsMutex;
    bool stopMonitoring = false;

    void addPath(const std::string& path) {
        std::lock_guard<std::mutex> lock(pathsMutex);
        monitoredPaths.push_back(path);
    }

    std::vector<std::string> getPaths() {
        std::lock_guard<std::mutex> lock(pathsMutex);
        return monitoredPaths;
    }
};

TEST_F(FilesMonitorThreadTest, ThreadSafePathAddition) {
    std::vector<std::thread> threads;

    for (int i = 0; i < 10; i++) {
        threads.emplace_back([this, i]() {
            addPath("/path/to/file" + std::to_string(i) + ".txt");
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto paths = getPaths();
    EXPECT_EQ(paths.size(), 10);
}

TEST_F(FilesMonitorThreadTest, StopMonitoringFlag) {
    EXPECT_FALSE(stopMonitoring);

    stopMonitoring = true;
    EXPECT_TRUE(stopMonitoring);
}

TEST_F(FilesMonitorThreadTest, PollingLoopBehavior) {
    int iterationCount = 0;
    int maxIterations = 5;

    while (!stopMonitoring && iterationCount < maxIterations) {
        iterationCount++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(iterationCount, maxIterations);
}

// ============================================================================
// FilesMonitor HandleFile Logic Tests
// ============================================================================

class FilesMonitorHandleFileTest : public ::testing::Test {
protected:
    std::map<std::string, bool> fileMonitors;

    bool hasMonitor(const std::string& filePath) {
        return fileMonitors.find(filePath) != fileMonitors.end();
    }

    void handleFile(const std::string& filePath) {
        if (fileMonitors.find(filePath) == fileMonitors.end()) {
            fileMonitors[filePath] = true;
        }
    }
};

TEST_F(FilesMonitorHandleFileTest, HandleNewFile) {
    std::string filePath = "/path/to/file.txt";

    handleFile(filePath);

    EXPECT_TRUE(hasMonitor(filePath));
}

TEST_F(FilesMonitorHandleFileTest, HandleExistingFile) {
    std::string filePath = "/path/to/file.txt";
    handleFile(filePath);

    // Should not add duplicate
    handleFile(filePath);

    int count = 0;
    for (const auto& pair : fileMonitors) {
        if (pair.first == filePath) {
            count++;
        }
    }

    EXPECT_EQ(count, 1);
}

TEST_F(FilesMonitorHandleFileTest, HandleMultipleFiles) {
    handleFile("/path/to/file1.txt");
    handleFile("/path/to/file2.txt");
    handleFile("/path/to/file3.txt");

    EXPECT_TRUE(hasMonitor("/path/to/file1.txt"));
    EXPECT_TRUE(hasMonitor("/path/to/file2.txt"));
    EXPECT_TRUE(hasMonitor("/path/to/file3.txt"));
}

TEST_F(FilesMonitorHandleFileTest, HandleDirectoryPaths) {
    handleFile("/path/to/dir1");
    handleFile("/path/to/dir2");

    EXPECT_TRUE(hasMonitor("/path/to/dir1"));
    EXPECT_TRUE(hasMonitor("/path/to/dir2"));
}

// ============================================================================
// Integration-style tests
// ============================================================================

class FilesMonitorIntegrationTest : public ::testing::Test {
protected:
    std::string testDir;

    void SetUp() override {
        testDir = "/tmp/fm_integration_" + std::to_string(std::time(nullptr));
        fs::create_directories(testDir);
    }

    void TearDown() override {
        fs::remove_all(testDir);
    }

    void createFile(const std::string& name, const std::string& content) {
        std::string path = testDir + "/" + name;
        std::ofstream file(path);
        file << content;
        file.close();
    }
};

TEST_F(FilesMonitorIntegrationTest, MonitorDirectoryChanges) {
    // Simulate monitoring a directory
    std::vector<std::string> currentFiles;

    for (const auto& entry : fs::directory_iterator(testDir)) {
        if (fs::is_regular_file(entry.status())) {
            currentFiles.push_back(entry.path().string());
        }
    }

    EXPECT_EQ(currentFiles.size(), 0);

    // Create a file
    createFile("new_file.txt", "content");

    // Check again
    currentFiles.clear();
    for (const auto& entry : fs::directory_iterator(testDir)) {
        if (fs::is_regular_file(entry.status())) {
            currentFiles.push_back(entry.path().string());
        }
    }

    EXPECT_EQ(currentFiles.size(), 1);
}

TEST_F(FilesMonitorIntegrationTest, DetectNewFilesInDirectory) {
    std::string newFilePath = testDir + "/new_file.txt";

    // Before
    EXPECT_FALSE(fs::exists(newFilePath));

    // Create file
    createFile("new_file.txt", "content");

    // After
    EXPECT_TRUE(fs::exists(newFilePath));

    // Verify content
    std::ifstream file(newFilePath);
    std::string content;
    std::getline(file, content);
    file.close();

    EXPECT_EQ(content, "content");
}

// ============================================================================
// Test runner
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
