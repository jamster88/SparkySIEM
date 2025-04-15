/**
 * @file FilesMonitor.cpp
 * @brief Implementation of the FilesMonitor class for monitoring file changes.
 * 
 * This file contains the implementation of the FilesMonitor class, which is responsible
 * for monitoring a set of files and directories for changes. It uses the FileMonitor
 * class to handle individual file monitoring and publishes file change events to a
 * specified Kafka topic.
 * 
 * The FilesMonitor class operates in a separate thread, continuously checking the
 * specified paths for changes. It supports monitoring both individual files and
 * directories, and it dynamically updates the list of monitored files as files are
 * added or removed from the filesystem.
 * 
 * Key features:
 * - Monitors files and directories for changes.
 * - Dynamically adds new files to the monitoring list.
 * - Cleans up monitors for deleted files.
 * - Publishes file change events to a Kafka topic.
 * 
 * Dependencies:
 * - C++17 filesystem library for file and directory operations.
 * - Threading and synchronization primitives for concurrent monitoring.
 * - FileMonitor class for individual file monitoring.
 * 
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 * @todo Implement a mechanism to gracefully terminate the monitoring thread.
 * @todo Add error handling for Kafka message sending failures.
 */
#include "FilesMonitor.h"   // Include the existing FilesMonitor header
#include <filesystem>       // Used for std::filesystem
#include <memory>           // Used for std::unique_ptr
#include <thread>           // Used for std::thread
#include <chrono>           // Used for timestamp generation
#include <mutex>            // Used for std::mutex
#include <vector>           // Used for std::vector
#include <unordered_map>    // Used for std::unordered_map
#include <iostream>         // Used for std::cerr
#include "FileMonitor.h"    // Include the existing FileMonitor header

namespace fs = std::filesystem;

/**
 * @brief Constructs a FilesMonitor object and starts a monitoring thread.
 * 
 * @param pathsToMonitor A vector of file paths to monitor for changes.
 * @param kafkaTopic The Kafka topic to which file change events will be published.
 */
FilesMonitor::FilesMonitor(const std::vector<std::string>& pathsToMonitor, const std::string& kafkaTopic)
    : paths(pathsToMonitor), topic(kafkaTopic), stopMonitoring(false) {
    monitorThread = std::thread(&FilesMonitor::monitorLoop, this);
}

/**
 * @brief Destructor for the FilesMonitor class.
 *
 * This destructor ensures that the file monitoring process is stopped
 * gracefully. It sets the `stopMonitoring` flag to true, signaling the
 * monitoring thread to terminate. If the monitoring thread is still
 * joinable, it waits for the thread to finish execution by calling `join()`.
 */
FilesMonitor::~FilesMonitor() {
    stopMonitoring = true;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
}


/**
 * @brief The main loop for monitoring files and directories.
 *
 * This method continuously checks the specified paths for changes and
 * handles file modifications. It also cleans up deleted files from the
 * monitored list.
 */
void FilesMonitor::monitorLoop() {
    while (!stopMonitoring) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(monitorMutex);

        for (const auto& path : paths) {
            if (fs::exists(path)) {
                if (fs::is_directory(path)) {
                    for (const auto& entry : fs::directory_iterator(path)) {
                        handleFile(entry.path().string());
                    }
                } else {
                    handleFile(path);
                }
            }
        }

        cleanupDeletedFiles();
    }
}

/**
 * @brief Handles the monitoring of a specific file.
 * 
 * This function checks if the given file is already being monitored. If not, 
 * it creates a new FileMonitor instance for the file and adds it to the 
 * fileMonitors map.
 * 
 * @param filePath The path of the file to be monitored.
 */
void FilesMonitor::handleFile(const std::string& filePath) {
    if (fileMonitors.find(filePath) == fileMonitors.end()) {
        fileMonitors[filePath] = std::make_unique<FileMonitor>(filePath, topic);
    }
}

/**
 * @brief Cleans up the file monitors by removing entries for files that no longer exist.
 * 
 * This function iterates through the `fileMonitors` container and checks if the file
 * corresponding to each entry still exists in the filesystem. If a file does not exist,
 * its associated entry is removed from the `fileMonitors` container.
 * 
 * @note The function uses an iterator to safely remove elements from the container
 *       while iterating over it.
 */
void FilesMonitor::cleanupDeletedFiles() {
    for (auto it = fileMonitors.begin(); it != fileMonitors.end();) {
        if (!fs::exists(it->first)) {
            it = fileMonitors.erase(it);
        } else {
            ++it;
        }
    }
}