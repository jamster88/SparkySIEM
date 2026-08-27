/**
 * @class FileMonitor
 * @brief A class for monitoring changes to a file and forwarding them to a MessageSink.
 *
 * The FileMonitor class watches a single file with inotify and publishes the content
 * that is appended to it. Existing content is published once when monitoring starts,
 * and from then on only newly written lines are sent, so a growing log file does not
 * get resent from the beginning on every change.
 *
 * @details
 * - Construct with a file path plus either a Kafka broker/topic pair or a MessageSink.
 * - monitor() blocks until stop() is called from another thread or a signal handler.
 * - Partial lines are buffered until their terminating newline arrives, so a reader
 *   never sees half a log record.
 * - Truncation is detected (the file shrinks) and reading restarts from the beginning.
 * - Rotation is detected (a new file replaces the path) and the watch is re-attached,
 *   which is why the parent directory is watched alongside the file itself.
 *
 * @note
 * - The inotify API is Linux specific, so this class does not build on macOS or Windows.
 * - The class is non-copyable: it owns file descriptors and an inotify watch.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 * @warning monitor() publishes the file's existing content before it starts following
 *          the file. Point it at a file whose backlog you actually want forwarded.
 */

#ifndef FILEMONITOR_H
#define FILEMONITOR_H

#include <atomic>
#include <memory>
#include <string>
#include <sys/types.h>

#include "MessageSink.h"

/**
 * @brief Monitors a file for changes and forwards them to a MessageSink.
 */
class FileMonitor {
public:
    /**
     * @brief Constructs a FileMonitor that publishes to a Kafka topic.
     * @param filePath The path of the file to monitor.
     * @param kafkaBroker The address of the Kafka broker.
     * @param kafkaTopic The Kafka topic to which messages will be sent.
     * @throws std::runtime_error If Kafka setup or the inotify watch fails.
     */
    FileMonitor(const std::string& filePath,
                const std::string& kafkaBroker,
                const std::string& kafkaTopic);

    /**
     * @brief Constructs a FileMonitor that publishes to a caller-supplied sink.
     * @param filePath The path of the file to monitor.
     * @param sink The destination for the messages. Must not be null.
     * @param topicName The topic name echoed back in each message body.
     * @throws std::invalid_argument If the sink is null.
     * @throws std::runtime_error If the file cannot be watched.
     */
    FileMonitor(const std::string& filePath,
                std::shared_ptr<MessageSink> sink,
                const std::string& topicName);

    /**
     * @brief Stops monitoring and releases the inotify resources.
     * @warning If monitor() is running on another thread, stop it and join that thread
     *          before destroying the object. The descriptors are closed here.
     */
    ~FileMonitor();

    FileMonitor(const FileMonitor&) = delete;
    FileMonitor& operator=(const FileMonitor&) = delete;

    /**
     * @brief Monitors the file until stop() is called.
     *
     * Publishes an "INIT" message, the file's current content, then every line that is
     * appended afterwards. Publishes a "CLOSE" message and flushes the sink on the way
     * out, so a clean shutdown does not drop buffered messages.
     */
    void monitor();

    /**
     * @brief Asks monitor() to return. Safe to call from any thread, and idempotent.
     */
    void stop();

    /**
     * @brief Reports whether monitor() is currently running.
     * @return True between the start and the end of monitor().
     */
    bool isRunning() const;

    /**
     * @brief The path this instance watches.
     * @return The monitored file path.
     */
    const std::string& path() const { return filePath; }

private:
    /// Formats and publishes one message, swallowing sink failures so the loop survives.
    void emit(const std::string& line, const std::string& messageType);

    /// Reads everything appended since the last read and publishes the complete lines.
    void readNewData();

    /// Publishes any complete lines currently sitting in the partial-line buffer.
    void flushCompleteLines();

    /// Re-attaches the watch if a different file now occupies the monitored path.
    void checkForReplacement();

    /// Drains and interprets one batch of inotify events.
    void processInotifyEvents();

    /// Closes any descriptors this instance opened.
    void closeDescriptors();

    std::string filePath;              ///< The path of the file being monitored.
    std::string directoryPath;         ///< The parent directory, watched to catch rotation.
    std::string fileName;              ///< The file name within that directory.
    std::string topicName;             ///< The topic echoed back in each message.
    std::shared_ptr<MessageSink> sink; ///< Destination for published messages.

    int inotifyFd = -1;  ///< File descriptor for the inotify instance.
    int fileWatch = -1;  ///< Watch descriptor for the monitored file.
    int dirWatch = -1;   ///< Watch descriptor for the parent directory.
    int stopFd = -1;     ///< eventfd used to wake the poll loop on stop().

    std::atomic<bool> stopRequested{false}; ///< Set by stop().
    std::atomic<bool> running{false};       ///< True while monitor() is executing.

    off_t offset = 0;             ///< How far into the file we have already read.
    ino_t watchedInode = 0;       ///< Inode currently watched, used to detect rotation.
    std::string partialLine;      ///< Bytes read that do not yet end in a newline.
    bool openErrorReported = false; ///< Prevents repeating the same open failure.
};

#endif
