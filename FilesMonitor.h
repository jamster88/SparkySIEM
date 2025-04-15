#ifndef FILESMONITOR_H
#define FILESMONITOR_H

#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include "FileMonitor.h" // Include the existing FileMonitor header

namespace fs = std::filesystem;

/**
 * @class FilesMonitor
 * @brief Monitors multiple files and directories for changes and sends notifications to a Kafka topic.
 *
 * The FilesMonitor class uses inotify to monitor specified files and directories for changes
 * and sends formatted messages to a Kafka topic using the provided Kafka broker.
 */
class FilesMonitor {
public:
    /**
     * @brief Constructs a FilesMonitor object.
     * @param pathsToMonitor A vector of file and directory paths to monitor.
     * @param kafkaTopic The Kafka topic to which messages will be sent.
     */
    FilesMonitor(const std::vector<std::string>& pathsToMonitor, const std::string& kafkaTopic);

    /**
     * @brief Destroys the FilesMonitor object and releases resources.
     */
    ~FilesMonitor();
    
private:
    std::vector<std::string> paths; ///< Vector of file and directory paths to monitor
    std::string topic;              ///< Kafka topic to which messages will be sent
    std::unordered_map<std::string, std::unique_ptr<FileMonitor>> fileMonitors; ///< Map of file monitors
    std::thread monitorThread;      ///< Thread for monitoring files
    std::mutex monitorMutex;        ///< Mutex for thread safety
    bool stopMonitoring;            ///< Flag to stop monitoring

    /**
     * @brief The main loop for monitoring files and directories.
     */
    void monitorLoop();

    /**
     * @brief Handles a file or directory path.
     * @param filePath The path of the file or directory to handle.
     */
    void handleFile(const std::string& filePath);

    /**
     * @brief Cleans up deleted files from the monitored list.
     */
    void cleanupDeletedFiles();
};

#endif