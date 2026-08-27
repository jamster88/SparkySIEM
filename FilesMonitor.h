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
 * - stop() asks the scan and every file monitor to finish; the destructor joins them.
 * - Directory scanning is not recursive; nested directories are ignored.
 * - A path that does not exist yet is not an error; it is picked up when it appears.
 * - Every FileMonitor shares one MessageSink, so sinks must be thread safe.
 *
 * @par Concurrency
 * Three threads touch an instance: the caller's, the scanning thread, and one thread per
 * monitored file. Two mutexes keep them apart, and the rules below are what make the
 * shutdown path correct. They are easy to break by accident.
 * - @c monitorMutex guards @c fileMonitors and @c reportedFailures. Everything that
 *   touches either holds it.
 * - @c waitMutex pairs with @c waitCondition for the interruptible sleep between scans,
 *   and is the lock @c stopMonitoring is written under. Setting the flag without it
 *   allows the wake-up to be lost, which stalls shutdown for a whole scan interval.
 * - Nothing slow happens under @c monitorMutex. Joining a monitor thread is slow: it
 *   publishes a CLOSE and flushes the sink, up to a second for a real KafkaSink. Dead
 *   monitors are therefore taken out of the map under the lock and joined outside it,
 *   which is why takeDeletedFiles() and joinAll() are separate functions.
 * - Lock ordering: stop() releases @c waitMutex before taking @c monitorMutex, and the
 *   scanning thread releases @c monitorMutex before taking @c waitMutex. Neither is ever
 *   held while acquiring the other.
 * - @c stopMonitoring is checked again after @c monitorMutex is acquired, because stop()
 *   may have run in between. Skipping that check lets a scan start monitors that stop()
 *   has already walked past.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 * @warning Paths that cannot be watched are reported once and skipped, not retried loudly.
 * @warning One thread and one inotify instance per monitored file caps how many files an
 *          instance can follow; see Known Limitations in README.md.
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
     *
     * Once this returns, no further file is picked up, even by a scan that was already
     * in flight. Monitors that are already running publish their CLOSE message and wind
     * down on their own; the destructor joins their threads.
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

    /// Removes monitors whose files no longer exist and tells them to stop, returning
    /// them so the caller can join them. Caller must hold monitorMutex.
    std::vector<Watched> takeDeletedFiles();

    /// Forgets failures for paths that no longer exist. Caller must hold monitorMutex.
    void pruneReportedFailures();

    /// Joins the threads of monitors that have already been told to stop.
    static void joinAll(std::vector<Watched>& finished);

    /// Stops and joins every remaining file monitor.
    void stopAll();

    std::vector<std::string> paths;    ///< Files and directories to monitor.
    std::shared_ptr<MessageSink> sink; ///< Shared destination for all monitors.
    std::string topicName;             ///< Topic echoed back in each message.
    std::chrono::milliseconds scanInterval; ///< Gap between path scans.

    mutable std::mutex monitorMutex;                       ///< Guards the two members below.
    std::unordered_map<std::string, Watched> fileMonitors; ///< Active monitors, keyed by path.
    std::unordered_set<std::string> reportedFailures;      ///< Paths already reported as failing.

    std::mutex waitMutex;              ///< Pairs with waitCondition; stopMonitoring is set under it.
    std::condition_variable waitCondition; ///< Lets stop() cut a scan interval short.
    std::atomic<bool> stopMonitoring{false}; ///< Set by stop(); read by every loop.
    std::thread monitorThread;         ///< Thread running monitorLoop().
};

#endif
