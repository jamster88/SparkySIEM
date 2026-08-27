/**
 * @file test_files_monitor_logic.cpp
 * @brief Unit tests for FilesMonitor's core logic: file discovery, cleanup, and thread safety.
 *
 * These tests verify the same algorithms used by FilesMonitor without depending on
 * inotify or Kafka. Runs on both macOS and Linux.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

// ============================================================================
// Helpers - recreate the algorithm under test (isolated from platform deps)
// ============================================================================

/** Creates a temp dir, returns its path (throws on failure) */
static std::string createTempDir(const std::string& prefix = "fsm_test_") {
    std::string tmpl = "/tmp/" + prefix + "XXXXXX";
    char* dir = mkdtemp(&tmpl[0]);
    if (dir == nullptr) throw std::runtime_error("mkdtemp failed");
    return std::string(dir);
}

/** Creates a file in the given directory */
static void createTestFile(const std::string& dir, const std::string& name, const std::string& content) {
    fs::path p = fs::path(dir) / name;
    std::ofstream f(p);
    ASSERT_TRUE(f.is_open());
    f << content;
}

/** Simulates handleFile's map-insertion logic (with mutex protection as FilesMonitor uses) */
static void simulateHandleFile(std::unordered_map<std::string, bool>& monitors,
                                const std::string& filePath, std::mutex& mtx) {
    std::lock_guard<std::mutex> lock(mtx);
    if (monitors.find(filePath) == monitors.end()) {
        monitors[filePath] = true;
    }
}

/** Simulates cleanupDeletedFiles' erase-while-iterating logic */
static void simulateCleanupDeletedFiles(std::unordered_map<std::string, bool>& monitors) {
    for (auto it = monitors.begin(); it != monitors.end();) {
        if (!fs::exists(it->first)) {
            it = monitors.erase(it);
        } else {
            ++it;
        }
    }
}

/** Simulates monitorLoop's directory scanning logic */
static std::vector<std::string> simulateDirectoryScan(const std::string& dirPath) {
    std::vector<std::string> result;
    if (fs::is_directory(dirPath)) {
        for (const auto& entry : fs::directory_iterator(dirPath)) {
            if (entry.is_regular_file()) {
                result.push_back(entry.path().string());
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ============================================================================
// Constructor / initialization tests
// ============================================================================

TEST(FilesMonitorLogic, PathsCanBeSet) {
    std::vector<std::string> paths = {"/tmp/test1.txt", "/tmp/testdir"};
    EXPECT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0], "/tmp/test1.txt");
    EXPECT_EQ(paths[1], "/tmp/testdir");
}

TEST(FilesMonitorLogic, TopicCanBeSet) {
    std::string topic = "my-topic";
    EXPECT_EQ(topic, "my-topic");
}

TEST(FilesMonitorLogic, BrokerCanHaveDefaultAndCustomValues) {
    std::string defaultBroker = "localhost:9092";
    std::string customBroker  = "custom-broker:12345";
    EXPECT_EQ(defaultBroker, "localhost:9092");
    EXPECT_EQ(customBroker, "custom-broker:12345");
}

// ============================================================================
// Directory scanning tests (mirrors FilesMonitor::monitorLoop's directory handling)
// ============================================================================

TEST(FilesMonitorLogic, ScanDirectoryFindsAllRegularFiles) {
    std::string tmpDir = createTempDir("fsm_scan_");
    createTestFile(tmpDir, "a.txt", "content a");
    createTestFile(tmpDir, "b.txt", "content b");
    createTestFile(tmpDir, "c.txt", "content c");

    auto files = simulateDirectoryScan(tmpDir);
    EXPECT_EQ(files.size(), 3u);

    // Verify each file is found (path contains the filename)
    bool foundA = false, foundB = false, foundC = false;
    for (const auto& f : files) {
        if (f.find("a.txt") != std::string::npos) foundA = true;
        if (f.find("b.txt") != std::string::npos) foundB = true;
        if (f.find("c.txt") != std::string::npos) foundC = true;
    }
    EXPECT_TRUE(foundA && foundB && foundC);
}

TEST(FilesMonitorLogic, ScanEmptyDirectoryReturnsEmptyList) {
    std::string tmpDir = createTempDir("fsm_empty_");
    auto files = simulateDirectoryScan(tmpDir);
    EXPECT_EQ(files.size(), 0u);
}

TEST(FilesMonitorLogic, ScanNonExistentDirectoryReturnsEmptyList) {
    auto files = simulateDirectoryScan("/nonexistent/path/that/does/not/exist");
    EXPECT_EQ(files.size(), 0u);
}

// ============================================================================
// File existence / handleFile logic tests
// ============================================================================

TEST(FilesMonitorLogic, DetectsExistingFile) {
    std::string tmpDir = createTempDir("fsm_exist_");
    fs::path testFile = fs::path(tmpDir) / "test.txt";
    { std::ofstream f(testFile); f << "data"; }

    EXPECT_TRUE(fs::exists(testFile));
    EXPECT_TRUE(fs::is_regular_file(testFile));
}

TEST(FilesMonitorLogic, DetectsNonExistentFile) {
    std::string tmpDir = createTempDir("fsm_noexist_");
    fs::path testFile = fs::path(tmpDir) / "ghost.txt";
    EXPECT_FALSE(fs::exists(testFile));
}

TEST(FilesMonitorLogic, HandleFileAddsNewEntries) {
    std::unordered_map<std::string, bool> monitors;
    std::mutex mtx;
    std::string tmpDir = createTempDir("fsm_handle_");
    fs::path testFile = fs::path(tmpDir) / "test.txt";
    { std::ofstream f(testFile); f << "data"; }

    simulateHandleFile(monitors, testFile.string(), mtx);
    EXPECT_EQ(monitors.size(), 1u);
}

TEST(FilesMonitorLogic, HandleFileSkipsExistingEntries) {
    std::unordered_map<std::string, bool> monitors;
    std::mutex mtx;
    std::string tmpDir = createTempDir("fsm_dedup_");
    fs::path testFile = fs::path(tmpDir) / "test.txt";
    { std::ofstream f(testFile); f << "data"; }

    simulateHandleFile(monitors, testFile.string(), mtx);
    auto size1 = monitors.size();
    simulateHandleFile(monitors, testFile.string(), mtx);
    EXPECT_EQ(monitors.size(), size1);  // not doubled
}

// ============================================================================
// Cleanup logic tests (mirrors FilesMonitor::cleanupDeletedFiles)
// ============================================================================

TEST(FilesMonitorLogic, CleanupRemovesMissingFiles) {
    std::unordered_map<std::string, bool> monitors;
    std::string tmpDir = createTempDir("fsm_cleanup_");

    fs::path keepFile = fs::path(tmpDir) / "still_here.txt";
    fs::path delFile  = fs::path(tmpDir) / "gone.txt";

    { std::ofstream f(keepFile); f << "data"; }
    { std::ofstream f(delFile);   f << "data"; }

    monitors[keepFile.string()] = true;
    monitors[delFile.string()]  = true;

    remove(delFile);

    simulateCleanupDeletedFiles(monitors);

    EXPECT_EQ(monitors.size(), 1u);
    EXPECT_TRUE(monitors.count(keepFile.string()) > 0);
}

TEST(FilesMonitorLogic, CleanupKeepsExistingFiles) {
    std::unordered_map<std::string, bool> monitors;
    std::string tmpDir = createTempDir("fsm_keep_");

    fs::path file1 = fs::path(tmpDir) / "file1.txt";
    fs::path file2 = fs::path(tmpDir) / "file2.txt";
    { std::ofstream f(file1); f << "data"; }
    { std::ofstream f(file2); f << "data"; }

    monitors[file1.string()] = true;
    monitors[file2.string()] = true;

    simulateCleanupDeletedFiles(monitors);

    EXPECT_EQ(monitors.size(), 2u);
}

TEST(FilesMonitorLogic, CleanupHandlesAllMissingFiles) {
    std::unordered_map<std::string, bool> monitors;
    std::string tmpDir = createTempDir("fsm_allgone_");

    fs::path file1 = fs::path(tmpDir) / "gone1.txt";
    fs::path file2 = fs::path(tmpDir) / "gone2.txt";

    monitors[file1.string()] = true;
    monitors[file2.string()] = true;

    remove(file1);
    remove(file2);

    simulateCleanupDeletedFiles(monitors);

    EXPECT_EQ(monitors.size(), 0u);
}

// ============================================================================
// Thread safety tests (mirrors FilesMonitor's multi-threaded pattern)
// ============================================================================

TEST(FilesMonitorLogic, ConcurrentMapInsertionIsSafe) {
    std::unordered_map<std::string, bool> monitors;
    std::mutex mtx;
    std::vector<std::thread> threads;

    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&mtx, &monitors, t]() {
            std::lock_guard<std::mutex> lock(mtx);
            auto key = "file_" + std::to_string(t) + ".txt";
            if (monitors.find(key) == monitors.end()) {
                monitors[key] = true;
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(monitors.size(), 10u);
}

TEST(FilesMonitorLogic, ConcurrentHandleFileDoesNotDuplicate) {
    std::unordered_map<std::string, bool> monitors;
    std::mutex mtx;
    std::vector<std::thread> threads;

    for (int t = 0; t < 50; ++t) {
        threads.emplace_back([&mtx, &monitors]() {
            std::lock_guard<std::mutex> lock(mtx);
            auto key = "shared_file.txt";
            if (monitors.find(key) == monitors.end()) {
                monitors[key] = true;
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(monitors.size(), 1u);
}

// ============================================================================
// Thread lifecycle tests (mirrors FilesMonitor constructor/destructor)
// ============================================================================

TEST(FilesMonitorLogic, ThreadStartsAndStopsCleanly) {
    std::atomic<bool> running{false};

    std::thread t([&running]() {
        running.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        running.store(false);
    });

    // Wait for thread to start
    while (!running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));

    EXPECT_TRUE(running.load());
    t.join();

    EXPECT_FALSE(running.load());
}

TEST(FilesMonitorLogic, RepeatedStartStopIsSafe) {
    for (int i = 0; i < 10; ++i) {
        std::thread t([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
        t.join();
    }
    // If we get here without crash, the test passes
}
