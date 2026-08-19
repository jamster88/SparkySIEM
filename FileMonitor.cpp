#include "FileMonitor.h"

#include "KafkaSink.h"
#include "Message.h"

#include <sys/inotify.h> // Used for the inotify API
#include <sys/stat.h>    // Used for stat()
#include <fcntl.h>       // Used for fcntl() and O_NONBLOCK
#include <poll.h>        // Used for poll()
#include <unistd.h>      // Used for read(), write(), close(), pipe()
#include <climits>       // Used for NAME_MAX
#include <cerrno>        // Used for errno
#include <cstring>       // Used for strerror()
#include <fstream>       // Used for std::ifstream
#include <iostream>      // Used for std::cerr
#include <stdexcept>     // Used for std::runtime_error
#include <vector>        // Used for the read buffer

namespace {

/// Events we care about on the monitored file itself.
constexpr std::uint32_t kFileMask = IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF;
/// Events we care about in the parent directory, so a replaced file is picked up.
constexpr std::uint32_t kDirMask = IN_CREATE | IN_MOVED_TO;
/// Room for several events at once; an event's name can be up to NAME_MAX bytes.
constexpr std::size_t kEventBufSize = 16 * (sizeof(struct inotify_event) + NAME_MAX + 1);
/// How long poll() waits before looping to re-check the running flag, in ms.
constexpr int kPollTimeoutMs = 200;
/// How long the sink is given to deliver outstanding messages at shutdown, in ms.
constexpr int kShutdownFlushMs = 5000;
/// Size of the chunks the monitored file is read in.
constexpr std::size_t kReadChunkSize = 64 * 1024;
/// Placeholder payload for messages that carry no file content.
const char* const kNoContent = " ";

/**
 * @brief Puts a descriptor into non-blocking mode.
 *
 * Both the inotify descriptor and the self-pipe are drained with a read loop that
 * stops when there is nothing left. On a blocking descriptor that final read would
 * never return, so non-blocking mode is what makes those loops terminate. It also
 * means stop() cannot block on a full pipe, which matters when it is called from a
 * signal handler.
 */
bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/// Splits a path into its directory and base name components.
void splitPath(const std::string& path, std::string& dir, std::string& name) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        dir = ".";
        name = path;
    } else if (slash == 0) {
        dir = "/";
        name = path.substr(1);
    } else {
        dir = path.substr(0, slash);
        name = path.substr(slash + 1);
    }
}

} // namespace

/**
 * @brief Constructs a FileMonitor that forwards to a Kafka topic.
 */
FileMonitor::FileMonitor(const std::string& filePath,
                         const std::string& kafkaBroker,
                         const std::string& kafkaTopic)
    : FileMonitor(filePath,
                  std::unique_ptr<MessageSink>(new KafkaSink(kafkaBroker, kafkaTopic)),
                  kafkaTopic) {}

/**
 * @brief Constructs a FileMonitor that forwards to a caller-supplied sink.
 *
 * Sets up the inotify instance, the watch on the file, a best-effort watch on the
 * parent directory (used to notice a replacement file after log rotation), and
 * the self-pipe that stop() uses to wake the monitor loop. The starting offset is
 * the file's current size, so only content appended from now on is forwarded.
 *
 * Everything fallible runs inside a try/catch that calls cleanup() before
 * rethrowing, because a constructor that throws leaves the destructor uncalled.
 */
FileMonitor::FileMonitor(const std::string& filePath,
                         std::unique_ptr<MessageSink> sink,
                         const std::string& topicLabel)
    : filePath(filePath), kafkaTopic(topicLabel), sink(std::move(sink)),
      inotifyFd(-1), fileWatchFd(-1), dirWatchFd(-1), stopPipe{-1, -1},
      running(false), lastOffset(0), lastInode(0) {
    if (!this->sink) {
        throw std::invalid_argument("FileMonitor requires a non-null MessageSink");
    }
    splitPath(filePath, dirPath, fileName);

    try {
        inotifyFd = inotify_init();
        if (inotifyFd < 0) {
            throw std::runtime_error("Failed to initialize inotify: " +
                                     std::string(strerror(errno)));
        }
        if (!setNonBlocking(inotifyFd)) {
            throw std::runtime_error("Failed to set inotify descriptor non-blocking: " +
                                     std::string(strerror(errno)));
        }

        fileWatchFd = inotify_add_watch(inotifyFd, filePath.c_str(), kFileMask);
        if (fileWatchFd < 0) {
            throw std::runtime_error("Failed to add inotify watch: " +
                                     std::string(strerror(errno)));
        }

        // Rotation detection is a bonus, not a requirement: if the directory
        // cannot be watched we still tail the file we already have.
        dirWatchFd = inotify_add_watch(inotifyFd, dirPath.c_str(), kDirMask);
        if (dirWatchFd < 0) {
            std::cerr << "Warning: cannot watch directory " << dirPath << " ("
                      << strerror(errno) << "); a replaced file will not be picked up"
                      << std::endl;
        }

        if (pipe(stopPipe) != 0) {
            stopPipe[0] = stopPipe[1] = -1;
            throw std::runtime_error("Failed to create stop pipe: " +
                                     std::string(strerror(errno)));
        }
        if (!setNonBlocking(stopPipe[0]) || !setNonBlocking(stopPipe[1])) {
            throw std::runtime_error("Failed to set stop pipe non-blocking: " +
                                     std::string(strerror(errno)));
        }

        // Start at the end of the file: we forward changes, not history.
        struct stat st{};
        if (stat(filePath.c_str(), &st) == 0) {
            lastOffset = static_cast<std::streamoff>(st.st_size);
            lastInode = static_cast<std::uint64_t>(st.st_ino);
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

/**
 * @brief Destroys the FileMonitor and releases the inotify watches and pipe.
 */
FileMonitor::~FileMonitor() {
    cleanup();
}

/**
 * @brief Releases the inotify watches, inotify fd and self-pipe.
 *
 * Safe to call more than once, and safe to call on a partially constructed
 * object: every descriptor is checked and then reset.
 */
void FileMonitor::cleanup() {
    if (inotifyFd >= 0) {
        if (fileWatchFd >= 0) {
            inotify_rm_watch(inotifyFd, fileWatchFd);
            fileWatchFd = -1;
        }
        if (dirWatchFd >= 0) {
            inotify_rm_watch(inotifyFd, dirWatchFd);
            dirWatchFd = -1;
        }
        close(inotifyFd);
        inotifyFd = -1;
    }
    for (int& fd : stopPipe) {
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }
}

/**
 * @brief Sends one message, formatted with the current timestamp.
 *
 * A sink is not supposed to throw, but a failure here must not be allowed to
 * abort the monitor loop, so the call is guarded.
 */
void FileMonitor::emit(const std::string& line, const std::string& type) {
    try {
        sink->send(sparky::formatMessage(sparky::currentTimestamp(), filePath,
                                         kafkaTopic, line, type));
    } catch (const std::exception& e) {
        std::cerr << "Error sending message to sink: " << e.what() << std::endl;
    }
}

/**
 * @brief Monitors the file until stop() is called.
 *
 * Waits on both the inotify descriptor and the stop pipe, so a stop request is
 * acted on immediately rather than after the next file change. Because the loop
 * has a real exit, the closing "CLOSE" message and the flush at the end are
 * actually reached.
 */
void FileMonitor::monitor() {
    running.store(true);
    emit(kNoContent, "INIT");

    // Confirm the file can actually be read before claiming it is open.
    {
        std::ifstream probe(filePath, std::ios::binary);
        if (probe.is_open()) {
            emit(kNoContent, "INIT - FILE OPEN");
        } else {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            emit(kNoContent, "ERROR - FILE OPEN");
        }
    }

    while (running.load()) {
        struct pollfd fds[2];
        fds[0].fd = inotifyFd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = stopPipe[0];
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        const int ready = poll(fds, 2, kPollTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue; // A signal arrived; re-check the running flag.
            }
            std::cerr << "poll() failed: " << strerror(errno) << std::endl;
            break; // Not recoverable, and looping here would spin.
        }
        if (ready == 0) {
            continue; // Timed out; loop to re-check the running flag.
        }

        if (fds[1].revents & POLLIN) {
            char discard[64];
            // The pipe is non-blocking, so this ends with EAGAIN rather than
            // waiting for a byte that is never coming.
            while (read(stopPipe[0], discard, sizeof(discard)) > 0) {
                // Drain the wake-up byte(s).
            }
            break;
        }
        if (fds[0].revents & POLLIN) {
            readEvents();
        }
    }

    drainNewLines(); // Pick up anything written just before shutdown.
    emit(kNoContent, "CLOSE");
    sink->flush(kShutdownFlushMs);
    running.store(false);
}

/**
 * @brief Asks monitor() to return.
 */
void FileMonitor::stop() {
    running.store(false);
    if (stopPipe[1] >= 0) {
        const char wake = 1;
        // write() is async-signal-safe, which is what makes stop() usable from
        // a signal handler. The result is deliberately ignored: if the pipe is
        // full the loop is already about to wake up.
        const ssize_t written = write(stopPipe[1], &wake, 1);
        static_cast<void>(written);
    }
}

/**
 * @brief Reads and dispatches whatever inotify events are pending.
 *
 * The offset of the next event is computed before the current one is handled and
 * the cursor is advanced unconditionally, so no handler can accidentally cause
 * the same event to be processed again.
 */
void FileMonitor::readEvents() {
    alignas(struct inotify_event) char buffer[kEventBufSize];

    const ssize_t length = read(inotifyFd, buffer, sizeof(buffer));
    if (length <= 0) {
        if (length < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            std::cerr << "Error reading inotify events: " << strerror(errno) << std::endl;
        }
        return;
    }

    ssize_t i = 0;
    while (i + static_cast<ssize_t>(sizeof(struct inotify_event)) <= length) {
        const struct inotify_event* event =
            reinterpret_cast<const struct inotify_event*>(buffer + i);
        const ssize_t next =
            i + static_cast<ssize_t>(sizeof(struct inotify_event)) + event->len;

        if (event->wd == fileWatchFd) {
            handleFileEvent(event->mask);
        } else if (event->wd == dirWatchFd && event->len > 0 &&
                   fileName == event->name && (event->mask & kDirMask)) {
            tryRewatch();
        }

        i = next;
    }
}

/**
 * @brief Handles an event on the watched file.
 */
void FileMonitor::handleFileEvent(std::uint32_t mask) {
    if (mask & IN_MODIFY) {
        drainNewLines();
    }
    if (mask & (IN_MOVE_SELF | IN_DELETE_SELF)) {
        handleRotation();
    }
    if (mask & IN_IGNORED) {
        // The kernel has already discarded this watch.
        fileWatchFd = -1;
    }
}

/**
 * @brief Drops the watch on a file that was renamed or deleted.
 *
 * Without this, the watch keeps following the old inode, so writes to the rotated
 * file would be reported as changes to the monitored path while genuine writes to
 * the new file went unnoticed. After reporting the rotation we immediately try to
 * pick up a replacement, since a rotate-and-recreate often happens faster than
 * the directory event is delivered.
 */
void FileMonitor::handleRotation() {
    emit(kNoContent, "ROTATE");

    if (fileWatchFd >= 0 && inotifyFd >= 0) {
        inotify_rm_watch(inotifyFd, fileWatchFd);
        fileWatchFd = -1;
    }
    lastOffset = 0;
    partialLine.clear();

    tryRewatch();
}

/**
 * @brief Re-establishes the file watch once the path exists again.
 *
 * Idempotent: if the file is already watched, or the path does not exist yet,
 * this does nothing and the directory watch will call it again later.
 */
void FileMonitor::tryRewatch() {
    if (fileWatchFd >= 0 || inotifyFd < 0) {
        return;
    }

    const int wd = inotify_add_watch(inotifyFd, filePath.c_str(), kFileMask);
    if (wd < 0) {
        return; // Not there yet; the directory watch will tell us when it is.
    }

    fileWatchFd = wd;
    lastOffset = 0; // A replacement file is read from the beginning.
    partialLine.clear();

    // Adopt the new inode here so the drain below does not report the
    // replacement a second time as a rotation.
    struct stat st{};
    lastInode = (stat(filePath.c_str(), &st) == 0) ? static_cast<std::uint64_t>(st.st_ino) : 0;

    emit(kNoContent, "REWATCH");
    drainNewLines();
}

/**
 * @brief Reads everything appended since the last read and emits its lines.
 *
 * Only the bytes after the recorded offset are read, which is what makes a
 * single appended line produce a single message instead of a replay of the whole
 * file. A file that has shrunk is treated as truncated and read again from the
 * start.
 */
void FileMonitor::drainNewLines() {
    struct stat st{};
    if (stat(filePath.c_str(), &st) != 0) {
        std::cerr << "Failed to stat file: " << filePath << " (" << strerror(errno) << ")"
                  << std::endl;
        emit(kNoContent, "ERROR - FILE STAT");
        return;
    }

    const std::streamoff size = static_cast<std::streamoff>(st.st_size);
    const std::uint64_t inode = static_cast<std::uint64_t>(st.st_ino);

    if (lastInode != 0 && inode != lastInode) {
        // Something was moved or copied into place over the file we were reading.
        // The offset from the old inode means nothing here, so start over.
        emit(kNoContent, "ROTATE");
        lastOffset = 0;
        partialLine.clear();
    } else if (size < lastOffset) {
        emit(kNoContent, "TRUNCATE");
        lastOffset = 0;
        partialLine.clear();
    }
    lastInode = inode;

    if (size == lastOffset) {
        return; // Nothing new; the event was not about content.
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        emit(kNoContent, "ERROR - FILE OPEN");
        return;
    }

    file.seekg(lastOffset);
    if (!file) {
        std::cerr << "Failed to seek in file: " << filePath << std::endl;
        emit(kNoContent, "ERROR - FILE OPEN");
        return;
    }

    std::vector<char> chunk(kReadChunkSize);
    while (file.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) ||
           file.gcount() > 0) {
        const std::streamsize got = file.gcount();
        consumeChunk(chunk.data(), static_cast<std::size_t>(got));
        lastOffset += got;
    }
}

/**
 * @brief Splits a freshly read chunk into complete lines and emits them.
 *
 * Bytes that do not yet end in a newline are kept in partialLine and prepended to
 * the next chunk, so a line that is written in two pieces is still delivered as
 * one message.
 */
void FileMonitor::consumeChunk(const char* data, std::size_t length) {
    partialLine.append(data, length);

    std::size_t start = 0;
    for (;;) {
        const std::size_t newline = partialLine.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }

        std::string line = partialLine.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // Tolerate CRLF-terminated files.
        }
        emit(line, "MODIFY");
        start = newline + 1;
    }
    partialLine.erase(0, start);
}
