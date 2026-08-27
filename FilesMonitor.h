/**
 * @class FilesMonitor
 * @brief A class for monitoring files and directories and forwarding changes to a sink.
 *
 * The FilesMonitor class watches a list of files and directories. A background thread
 * rescans the list on an interval, and every regular file it finds gets its own
 * FileMonitor running on its own thread, so several files are followed at once. Files
 * that appear in a watched directory are picked up on the next scan, and files that
 * disappear have their monitor stopped and joined.
 *
 * @note This class is not copyable: it owns threads and per-file monitors.
 *
 * @details
 * - The constructor starts the scanning thread immediately.
 * - stop() and the destructor shut down every monitor thread and join it.
 * - Directory scanning is not recursive; nested directories are ignored.
 * - Every FileMonitor shares one MessageSink, so sinks must be thread safe.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 * @warning Paths that cannot be watched are reported once and skipped, not retried loudly.
 */

#ifndef FILESMONITOR_H
#define FILESMONITOR_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "FileMonitor.h"
#include "MessageSink.h"

/**
 * @brief Monitors multiple files and directories and forwards their changes.
 */
class FilesMonitor {
public:
    /// Default gap between scans of the configured paths.
    static constexpr std::chrono::milliseconds kDefaultScanInterval{1000};

    /**
     * @brief Constructs a FilesMonitor that publishes to a Kafka topic.
     * @param pathsToMonitor Files and directories to monitor.
     * @param kafkaBroker The address of the Kafka broker.
     * @param kafkaTopic The Kafka topic to which messages will be sent.
     * @param scanInterval How often the paths are rescanned for new or deleted files.
     * @throws std::runtime_error If the Kafka producer cannot be created.
     */
    FilesMonitor(const std::vector<std::string>& pathsToMonitor,
                 const std::string& kafkaBroker,
                 const std::string& kafkaTopic,
                 std::chrono::milliseconds scanInterval = kDefaultScanInterval);

    /**
     * @brief Constructs a FilesMonitor that publishes to a caller-supplied sink.
     * @param pathsToMonitor Files and directories to monitor.
     * @param sink The destination for the messages. Must not be null.
     * @param topicName The topic name echoed back in each message body.
     * @param scanInterval How often the paths are rescanned for new or deleted files.
     * @throws std::invalid_argument If the sink is null.
     */
    FilesMonitor(const std::vector<std::string>& pathsToMonitor,
                 std::shared_ptr<MessageSink> sink,
                 const std::string& topicName,
                 std::chrono::milliseconds scanInterval = kDefaultScanInterval);

    /**
     * @brief Stops every monitor and joins every thread.
     */
    ~FilesMonitor();

    FilesMonitor(const FilesMonitor&) = delete;
    FilesMonitor& operator=(const FilesMonitor&) = delete;

    /**
     * @brief Signals the scanning thread and all file monitors to stop. Idempotent.
     */
    void stop();

    /**
     * @brief Lists the files currently being monitored.
     * @return The monitored file paths, in unspecified order.
     */
    std::vector<std::string> monitoredFiles() const;

private:
    /// One monitored file: the monitor plus the thread running its loop.
    struct Watched {
        std::unique_ptr<FileMonitor> monitor;
        std::thread worker;
    };

    /// The scanning loop that runs on monitorThread.
    void monitorLoop();

    /// Walks the configured paths once. Caller must hold monitorMutex.
    void scanPaths();

    /// Starts a monitor for a file if one is not already running. Caller holds monitorMutex.
    void handleFile(const std::string& filePath);

    /// Stops and joins monitors whose files no longer exist. Caller holds monitorMutex.
    void cleanupDeletedFiles();

    /// Stops and joins every remaining file monitor.
    void stopAll();

    std::vector<std::string> paths;    ///< Files and directories to monitor.
    std::shared_ptr<MessageSink> sink; ///< Shared destination for all monitors.
    std::string topicName;             ///< Topic echoed back in each message.
    std::chrono::milliseconds scanInterval; ///< Gap between path scans.

    mutable std::mutex monitorMutex;                       ///< Guards fileMonitors.
    std::unordered_map<std::string, Watched> fileMonitors; ///< Active monitors by path.
    std::unordered_set<std::string> reportedFailures;      ///< Paths already reported as failing.

    std::mutex waitMutex;              ///< Pairs with waitCondition for interruptible sleep.
    std::condition_variable waitCondition; ///< Lets stop() cut a scan interval short.
    std::atomic<bool> stopMonitoring{false}; ///< Flag to stop monitoring.
    std::thread monitorThread;         ///< Thread running monitorLoop().
};

#endif
