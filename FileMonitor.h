#ifndef FILEMONITOR_H
#define FILEMONITOR_H

#include "MessageSink.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <memory>
#include <string>

/**
 * @class FileMonitor
 * @brief Tails a file with inotify and forwards newly appended lines to a MessageSink.
 *
 * The monitor records the file's size when it is constructed and from then on
 * reports only what is added after that point, so restarting it does not replay
 * content that has already been forwarded. Each complete line becomes one
 * "MODIFY" message; a trailing partial line is held back until its newline
 * arrives, so a line is never split across two messages.
 *
 * Lifecycle events are reported as their own message types: "INIT" and
 * "INIT - FILE OPEN" at startup, "TRUNCATE" when the file shrinks, "ROTATE" and
 * "REWATCH" when the file is renamed or replaced underneath us,
 * "ERROR - FILE OPEN" / "ERROR - FILE STAT" when it cannot be read, and "CLOSE"
 * on shutdown.
 *
 * Only Linux is supported, because the implementation depends on inotify.
 */
class FileMonitor {
public:
    /**
     * @brief Constructs a FileMonitor that forwards to a Kafka topic.
     * @param filePath The path of the file to monitor.
     * @param kafkaBroker The address of the Kafka broker.
     * @param kafkaTopic The Kafka topic to which messages will be sent.
     * @throws std::runtime_error If the Kafka producer cannot be created, or if
     *         the file cannot be watched (for example, it does not exist).
     */
    FileMonitor(const std::string& filePath,
                const std::string& kafkaBroker,
                const std::string& kafkaTopic);

    /**
     * @brief Constructs a FileMonitor that forwards to a caller-supplied sink.
     *
     * This is the seam the unit tests use: it allows the monitor loop to be
     * driven and observed without a Kafka broker.
     *
     * @param filePath The path of the file to monitor.
     * @param sink The destination for formatted messages; must not be null.
     * @param topicLabel The value reported in each message's "kafkaTopic" field.
     * @throws std::runtime_error If the file cannot be watched.
     * @throws std::invalid_argument If sink is null.
     */
    FileMonitor(const std::string& filePath,
                std::unique_ptr<MessageSink> sink,
                const std::string& topicLabel);

    /**
     * @brief Destroys the FileMonitor and releases the inotify watches and pipe.
     */
    ~FileMonitor();

    FileMonitor(const FileMonitor&) = delete;
    FileMonitor& operator=(const FileMonitor&) = delete;

    /**
     * @brief Monitors the file until stop() is called.
     *
     * Blocks the calling thread. On return, a "CLOSE" message has been sent and
     * the sink has been flushed.
     */
    void monitor();

    /**
     * @brief Asks monitor() to return.
     *
     * Safe to call from another thread or from a signal handler: it sets an
     * atomic flag and writes a single byte to a self-pipe, which wakes the
     * poll() in the monitor loop immediately.
     */
    void stop();

private:
    /** @brief Sends one message, formatted with the current timestamp. */
    void emit(const std::string& line, const std::string& type);

    /** @brief Reads and dispatches whatever inotify events are pending. */
    void readEvents();

    /** @brief Handles an event on the watched file. */
    void handleFileEvent(std::uint32_t mask);

    /**
     * @brief Reads everything appended since the last read and emits its lines.
     *
     * Detects truncation (file shorter than the recorded offset) and restarts
     * from the beginning of the file when it happens.
     */
    void drainNewLines();

    /** @brief Splits a freshly read chunk into complete lines and emits them. */
    void consumeChunk(const char* data, std::size_t length);

    /** @brief Drops the watch on a file that was renamed or deleted. */
    void handleRotation();

    /** @brief Re-establishes the file watch once the path exists again. */
    void tryRewatch();

    /** @brief Releases the inotify watches, inotify fd and self-pipe. */
    void cleanup();

    std::string filePath;                  ///< The path of the file being monitored.
    std::string dirPath;                   ///< Directory containing the file, watched for rotation.
    std::string fileName;                  ///< Base name of the file, matched against dir events.
    std::string kafkaTopic;                ///< Reported in the "kafkaTopic" field of each message.
    std::unique_ptr<MessageSink> sink;      ///< Where formatted messages are sent.
    int inotifyFd;                          ///< File descriptor for the inotify instance.
    int fileWatchFd;                        ///< Watch descriptor for the file, or -1 if unwatched.
    int dirWatchFd;                         ///< Watch descriptor for the parent directory, or -1.
    int stopPipe[2];                        ///< Self-pipe used by stop() to wake poll().
    std::atomic<bool> running;              ///< Cleared by stop() to end the monitor loop.
    std::streamoff lastOffset;              ///< Byte offset already forwarded.
    std::uint64_t lastInode;                ///< Inode last read from; a change means the file was replaced.
    std::string partialLine;                ///< Trailing bytes with no newline yet.
};

#endif
