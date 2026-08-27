/**
 * @file FilesMonitor.cpp
 * @brief Implementation of the FilesMonitor class.
 *
 * The scanning thread owns the list of active monitors. Each monitor gets a thread of
 * its own because FileMonitor::monitor() blocks until it is told to stop, so running
 * them inline would mean only the first file was ever followed.
 *
 * One pass of the loop is: take the lock, bail out if stopping, scan the configured paths
 * starting a monitor for anything new, move out the monitors whose files have gone away,
 * drop the lock, join those monitors, then sleep until the next interval or until stop()
 * cuts the sleep short.
 *
 * Shutdown happens in two halves, and both are needed. stop() sets the flag and asks
 * every monitor to finish, so the caller can trigger it from anywhere, including after a
 * signal; the destructor then joins the scanning thread and every monitor thread. That
 * split is why stop() alone leaves monitors running for a moment: they are publishing
 * their CLOSE and flushing the sink. The locking rules the two halves rely on are set out
 * under @par Concurrency in FilesMonitor.h; read them before changing anything here.
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
#include <stdexcept>   // Used for std::invalid_argument
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
    {
        // The flag is set while holding waitMutex so the scanning thread cannot be
        // between its predicate check and its wait when the notify below arrives. A
        // notify lost there would leave shutdown waiting out a whole scan interval.
        std::lock_guard<std::mutex> wait(waitMutex);
        stopMonitoring.store(true);
    }
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
        std::vector<Watched> finished;
        {
            std::lock_guard<std::mutex> lock(monitorMutex);
            // stop() may have run between the check above and this lock. Scanning now
            // would start monitors that stop() has already walked past, and they would
            // keep publishing until the destructor caught them.
            if (stopMonitoring.load()) {
                break;
            }
            scanPaths();
            finished = takeDeletedFiles();
            pruneReportedFailures();
        }

        // Join outside the lock. A monitor takes a moment to publish CLOSE and flush the
        // sink on the way out, and monitoredFiles() and stop() should not wait on that.
        joinAll(finished);

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
            // One level only, and regular files only. A nested directory is ignored, and
            // so is anything a read would block on or never end: a FIFO, a socket, a
            // device node. The non-throwing overloads are used throughout because a file
            // can disappear between the iterator producing it and the check running.
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
    // A scan already in flight when stop() is called must not start anything new.
    //
    // The key is the path as it was produced, so listing a directory and a file inside it
    // costs nothing: the directory scan yields the same string. Two different spellings of
    // one file (a symlink, or a path through "..") are not recognised as the same file,
    // and it is then monitored twice. See Known Limitations in README.md.
    if (stopMonitoring.load() || fileMonitors.find(filePath) != fileMonitors.end()) {
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

std::vector<FilesMonitor::Watched> FilesMonitor::takeDeletedFiles() {
    std::vector<Watched> finished;
    for (auto it = fileMonitors.begin(); it != fileMonitors.end();) {
        std::error_code ec;
        if (!fs::exists(it->first, ec) || ec) {
            // Ask it to stop while the lock is held, so every doomed monitor is winding
            // down in parallel by the time the caller starts joining them.
            it->second.monitor->stop();
            finished.push_back(std::move(it->second));
            it = fileMonitors.erase(it);
        } else {
            ++it;
        }
    }
    return finished;
}

void FilesMonitor::pruneReportedFailures() {
    // Without this the set grows for the life of the process in a directory that churns
    // unwatchable files, and a path that comes back is never reported again.
    for (auto it = reportedFailures.begin(); it != reportedFailures.end();) {
        std::error_code ec;
        if (!fs::exists(*it, ec) || ec) {
            it = reportedFailures.erase(it);
        } else {
            ++it;
        }
    }
}

void FilesMonitor::joinAll(std::vector<Watched>& finished) {
    for (auto& entry : finished) {
        if (entry.worker.joinable()) {
            entry.worker.join();
        }
    }
    // The monitors are destroyed with the vector, which is why the joins come first:
    // a FileMonitor must outlive the thread running its loop.
    finished.clear();
}

void FilesMonitor::stopAll() {
    // This is the one place that joins with monitorMutex held, and it is safe because the
    // destructor has already joined the scanning thread: nothing else can be waiting on
    // the lock. Every monitor is asked to stop before any of them is joined, so they wind
    // down in parallel rather than one flush at a time.
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
