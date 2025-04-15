#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include "FileMonitor.h" // Include the existing FileMonitor header

namespace fs = std::filesystem;

class FilesMonitor {
public:
    FilesMonitor(const std::vector<std::string>& pathsToMonitor, const std::string& kafkaTopic)
        : paths(pathsToMonitor), topic(kafkaTopic), stopMonitoring(false) {
        monitorThread = std::thread(&FilesMonitor::monitorLoop, this);
    }

    ~FilesMonitor() {
        stopMonitoring = true;
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
    }

private:
    std::vector<std::string> paths;
    std::string topic;
    std::unordered_map<std::string, std::unique_ptr<FileMonitor>> fileMonitors;
    std::thread monitorThread;
    std::mutex monitorMutex;
    bool stopMonitoring;

    void monitorLoop() {
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

    void handleFile(const std::string& filePath) {
        if (fileMonitors.find(filePath) == fileMonitors.end()) {
            fileMonitors[filePath] = std::make_unique<FileMonitor>(filePath, topic);
        }
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
