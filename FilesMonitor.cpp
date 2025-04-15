#include "FilesMonitor.h" // Include the existing FilesMonitor header
#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include <errno.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <librdkafka/rdkafkacpp.h> // Include the Kafka library header
#include <sys/inotify.h>           // Include the inotify header
#include <iostream>                // Include the iostream header for std::cerr
#include <fstream>                 // Include the fstream header for std::ifstream
#include <chrono>                  // Include the chrono header for timestamp generation
#include <stdexcept>               // Include the stdexcept header for std::runtime_error
#include <cstring>                 // Include the cstring header for strerror()
#include <sstream>                 // Include the sstream header for std::ostringstream
#include <iomanip>                 // Include the iomanip header for std::setfill and std::setw
#include <iostream>                // Include the iostream header for std::cerr
#include <fstream>                 // Include the fstream header for std::ifstream
#include <errno.h>                 // Include the errno header for errno
#include "FileMonitor.h" // Include the existing FileMonitor header

namespace fs = std::filesystem;

FilesMonitor::FilesMonitor(const std::vector<std::string>& pathsToMonitor, const std::string& kafkaTopic)
    : paths(pathsToMonitor), topic(kafkaTopic), stopMonitoring(false) {
    monitorThread = std::thread(&FilesMonitor::monitorLoop, this);
}

FilesMonitor::~FilesMonitor() {
    stopMonitoring = true;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
}


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