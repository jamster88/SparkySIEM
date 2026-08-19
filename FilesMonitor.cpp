/**
 * @file FilesMonitor.cpp
 * @brief Implementation of the FilesMonitor class.
 *
 * The scanning thread owns the list of active monitors. Each monitor gets a thread of
 * its own because FileMonitor::monitor() blocks until it is told to stop, so running
 * them inline would mean only the first file was ever followed.
 *
 * Dependencies:
 * - C++17 filesystem library for file and directory operations.
 * - Threading and synchronization primitives for concurrent monitoring.
 * - FileMonitor for the per-file work.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 */

#include "FilesMonitor.h"

#include <filesystem>  // Used for std::filesystem
#include <iostream>    // Used for std::cerr
#include <system_error>// Used for the non-throwing filesystem overloads
#include <utility>     // Used for std::move

#include "KafkaSink.h"

namespace fs = std::filesystem;

FilesMonitor::FilesMonitor(const std::vector<std::string>& pathsToMonitor,
                           const std::string& kafkaBroker,
                           const std::string& kafkaTopic,
                           std::chrono::milliseconds scanInterval)
    : FilesMonitor(pathsToMonitor,
                   std::make_shared<KafkaSink>(kafkaBroker, kafkaTopic),
                   kafkaTopic,
                   scanInterval) {}

FilesMonitor::FilesMonitor(const std::vector<std::string>& pathsToMonitor,
                           std::shared_ptr<MessageSink> sink,
                           const std::string& topicName,
                           std::chrono::milliseconds scanInterval)
    : paths(pathsToMonitor),
      sink(std::move(sink)),
      topicName(topicName),
      scanInterval(scanInterval) {
    if (!this->sink) {
        throw std::invalid_argument("FilesMonitor requires a non-null MessageSink");
    }
    monitorThread = std::thread(&FilesMonitor::monitorLoop, this);
}

FilesMonitor::~FilesMonitor() {
    stop();
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
    stopAll();
}

void FilesMonitor::stop() {
    stopMonitoring.store(true);
    waitCondition.notify_all();

    // Wake the per-file monitors too, so their threads can be joined without waiting
    // for the scanning thread to notice.
    std::lock_guard<std::mutex> lock(monitorMutex);
    for (auto& entry : fileMonitors) {
        entry.second.monitor->stop();
    }
}

void FilesMonitor::monitorLoop() {
    while (!stopMonitoring.load()) {
        {
            std::lock_guard<std::mutex> lock(monitorMutex);
            scanPaths();
            cleanupDeletedFiles();
        }

        // Sleep, but wake immediately if stop() is called mid-interval.
        std::unique_lock<std::mutex> wait(waitMutex);
        waitCondition.wait_for(wait, scanInterval, [this] { return stopMonitoring.load(); });
    }
}

void FilesMonitor::scanPaths() {
    for (const auto& path : paths) {
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) {
            continue;
        }

        if (fs::is_directory(path, ec) && !ec) {
            for (fs::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code entryEc;
                if (fs::is_regular_file(it->path(), entryEc) && !entryEc) {
                    handleFile(it->path().string());
                }
            }
        } else if (fs::is_regular_file(path, ec) && !ec) {
            handleFile(path);
        }
    }
}

void FilesMonitor::handleFile(const std::string& filePath) {
    if (fileMonitors.find(filePath) != fileMonitors.end()) {
        return;
    }

    try {
        Watched watched;
        watched.monitor = std::make_unique<FileMonitor>(filePath, sink, topicName);
        // monitor() blocks until stop(), so it needs a thread of its own.
        FileMonitor* monitor = watched.monitor.get();
        watched.worker = std::thread([monitor] { monitor->monitor(); });

        fileMonitors.emplace(filePath, std::move(watched));
        reportedFailures.erase(filePath);
    } catch (const std::exception& e) {
        // One unwatchable file must not stop the others from being monitored.
        if (reportedFailures.insert(filePath).second) {
            std::cerr << "Failed to monitor '" << filePath << "': " << e.what() << std::endl;
        }
    }
}

void FilesMonitor::cleanupDeletedFiles() {
    for (auto it = fileMonitors.begin(); it != fileMonitors.end();) {
        std::error_code ec;
        if (!fs::exists(it->first, ec) || ec) {
            it->second.monitor->stop();
            if (it->second.worker.joinable()) {
                it->second.worker.join();
            }
            it = fileMonitors.erase(it);
        } else {
            ++it;
        }
    }
}

void FilesMonitor::stopAll() {
    std::lock_guard<std::mutex> lock(monitorMutex);
    for (auto& entry : fileMonitors) {
        entry.second.monitor->stop();
    }
    for (auto& entry : fileMonitors) {
        if (entry.second.worker.joinable()) {
            entry.second.worker.join();
        }
    }
    fileMonitors.clear();
}

std::vector<std::string> FilesMonitor::monitoredFiles() const {
    std::lock_guard<std::mutex> lock(monitorMutex);
    std::vector<std::string> result;
    result.reserve(fileMonitors.size());
    for (const auto& entry : fileMonitors) {
        result.push_back(entry.first);
    }
    return result;
}
