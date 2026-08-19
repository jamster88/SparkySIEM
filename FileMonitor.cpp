/**
 * @file FileMonitor.cpp
 * @brief Implementation of the FileMonitor class.
 *
 * The monitor keeps a byte offset into the watched file. When inotify reports a
 * modification, only the bytes past that offset are read, split into complete lines,
 * and published. That is the difference between forwarding a file's changes and
 * forwarding the whole file over and over.
 *
 * The event loop waits in poll() on two descriptors: the inotify descriptor and an
 * eventfd that stop() writes to. That gives a prompt, race-free shutdown, and it means
 * a failure to read the file can never turn into a busy loop, because the loop only
 * advances when poll() reports something to do.
 *
 * Dependencies:
 * - Linux inotify and eventfd.
 * - C++17 filesystem library for splitting the path.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 */

#include "FileMonitor.h"

#include <algorithm>   // Used for std::min
#include <cerrno>      // Used for errno
#include <cstring>     // Used for strerror
#include <filesystem>  // Used to split the path into directory and file name
#include <fstream>     // Used for std::ifstream
#include <iostream>    // Used for std::cerr
#include <stdexcept>   // Used for std::runtime_error
#include <vector>      // Used for the inotify read buffer

#include <poll.h>          // Used for poll()
#include <sys/eventfd.h>   // Used for eventfd()
#include <sys/inotify.h>   // Used for inotify
#include <sys/stat.h>      // Used for stat()
#include <unistd.h>        // Used for read() and close()

#include "KafkaSink.h"
#include "MessageFormat.h"

namespace {
/// Events that matter on the monitored file itself.
constexpr uint32_t kFileEvents = IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF;
/// Events that matter on the parent directory: a replacement file appearing.
constexpr uint32_t kDirectoryEvents = IN_CREATE | IN_MOVED_TO;
/// How long poll() waits before looping, which also bounds the shutdown latency.
constexpr int kPollTimeoutMs = 200;
/// Size of one inotify read. Comfortably larger than a single event plus a name.
constexpr std::size_t kEventBufferSize = 8192;
/// Largest chunk read from the monitored file in one pass.
constexpr std::size_t kReadChunkSize = 64 * 1024;
/// A "line" longer than this is published as-is rather than buffered forever.
constexpr std::size_t kMaxLineBytes = 1024 * 1024;
/// Time allowed for the sink to drain when monitoring ends.
constexpr int kFinalFlushMs = 1000;
}  // namespace

FileMonitor::FileMonitor(const std::string& filePath,
                         const std::string& kafkaBroker,
                         const std::string& kafkaTopic)
    : FileMonitor(filePath, std::make_shared<KafkaSink>(kafkaBroker, kafkaTopic), kafkaTopic) {}

FileMonitor::FileMonitor(const std::string& filePath,
                         std::shared_ptr<MessageSink> sink,
                         const std::string& topicName)
    : filePath(filePath), topicName(topicName), sink(std::move(sink)) {
    if (!this->sink) {
        throw std::invalid_argument("FileMonitor requires a non-null MessageSink");
    }

    const std::filesystem::path path(filePath);
    directoryPath = path.has_parent_path() ? path.parent_path().string() : std::string(".");
    fileName = path.filename().string();

    inotifyFd = inotify_init1(IN_CLOEXEC);
    if (inotifyFd < 0) {
        throw std::runtime_error("Failed to initialize inotify: " + std::string(strerror(errno)));
    }

    stopFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (stopFd < 0) {
        const std::string reason(strerror(errno));
        closeDescriptors();
        throw std::runtime_error("Failed to create stop eventfd: " + reason);
    }

    struct stat fileStat {};
    if (::stat(filePath.c_str(), &fileStat) != 0) {
        const std::string reason(strerror(errno));
        closeDescriptors();
        throw std::runtime_error("Failed to stat file '" + filePath + "': " + reason);
    }
    watchedInode = fileStat.st_ino;

    fileWatch = inotify_add_watch(inotifyFd, filePath.c_str(), kFileEvents);
    if (fileWatch < 0) {
        const std::string reason(strerror(errno));
        closeDescriptors();
        throw std::runtime_error("Failed to add inotify watch: " + reason);
    }

    // Watching the directory is what lets us survive log rotation. It is a convenience
    // rather than a requirement, so a failure here is reported but not fatal.
    dirWatch = inotify_add_watch(inotifyFd, directoryPath.c_str(), kDirectoryEvents);
    if (dirWatch < 0) {
        std::cerr << "Warning: cannot watch directory '" << directoryPath
                  << "', rotation of '" << filePath << "' will not be detected: "
                  << strerror(errno) << std::endl;
    }
}

FileMonitor::~FileMonitor() {
    stop();
    closeDescriptors();
}

void FileMonitor::closeDescriptors() {
    if (inotifyFd >= 0) {
        if (fileWatch >= 0) {
            inotify_rm_watch(inotifyFd, fileWatch);
            fileWatch = -1;
        }
        if (dirWatch >= 0) {
            inotify_rm_watch(inotifyFd, dirWatch);
            dirWatch = -1;
        }
        close(inotifyFd);
        inotifyFd = -1;
    }
    if (stopFd >= 0) {
        close(stopFd);
        stopFd = -1;
    }
}

void FileMonitor::stop() {
    stopRequested.store(true);
    if (stopFd >= 0) {
        const uint64_t token = 1;
        // write() is async-signal-safe, so stop() may be called from a signal handler.
        const ssize_t written = ::write(stopFd, &token, sizeof(token));
        (void)written;
    }
}

bool FileMonitor::isRunning() const {
    return running.load();
}

void FileMonitor::emit(const std::string& line, const std::string& messageType) {
    try {
        sink->send(sparky::formatMessage(filePath, line, topicName, messageType,
                                         sparky::currentTimestamp()));
    } catch (const std::exception& e) {
        // A publish failure must not take the monitor down with it.
        std::cerr << "Error publishing message for '" << filePath << "': " << e.what() << std::endl;
    }
}

void FileMonitor::flushCompleteLines() {
    std::size_t start = 0;
    for (std::size_t newline = partialLine.find('\n', start); newline != std::string::npos;
         newline = partialLine.find('\n', start)) {
        std::string line = partialLine.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();  // tolerate CRLF files
        }
        emit(line, "MODIFY");
        start = newline + 1;
    }
    partialLine.erase(0, start);

    // Guard against a file that never writes a newline: publish rather than grow forever.
    if (partialLine.size() > kMaxLineBytes) {
        emit(partialLine, "MODIFY");
        partialLine.clear();
    }
}

void FileMonitor::readNewData() {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        if (!openErrorReported) {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            emit(" ", "ERROR - FILE OPEN");
            openErrorReported = true;  // report once, not once per event
        }
        return;
    }
    openErrorReported = false;

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return;
    }

    if (size < offset) {
        // The file shrank, so it was truncated underneath us. Start over.
        offset = 0;
        partialLine.clear();
        emit(" ", "TRUNCATED");
    }

    while (offset < size) {
        const std::size_t want =
            static_cast<std::size_t>(std::min<std::streamoff>(kReadChunkSize, size - offset));
        std::string chunk(want, '\0');

        file.seekg(offset, std::ios::beg);
        file.read(&chunk[0], static_cast<std::streamsize>(want));
        const std::streamsize got = file.gcount();
        if (got <= 0) {
            break;
        }
        chunk.resize(static_cast<std::size_t>(got));
        offset += got;

        partialLine += chunk;
        flushCompleteLines();
    }
}

void FileMonitor::checkForReplacement() {
    struct stat fileStat {};
    if (::stat(filePath.c_str(), &fileStat) != 0) {
        // Nothing at the path right now. The directory watch will tell us when that changes.
        return;
    }
    if (fileStat.st_ino == watchedInode) {
        return;
    }

    // A different file occupies the path, so the old watch is useless. Follow the new one.
    if (fileWatch >= 0) {
        inotify_rm_watch(inotifyFd, fileWatch);
        fileWatch = -1;
    }
    const int newWatch = inotify_add_watch(inotifyFd, filePath.c_str(), kFileEvents);
    if (newWatch < 0) {
        std::cerr << "Failed to re-watch '" << filePath << "': " << strerror(errno) << std::endl;
        return;
    }

    fileWatch = newWatch;
    watchedInode = fileStat.st_ino;
    offset = 0;
    partialLine.clear();
    openErrorReported = false;
    emit(" ", "ROTATED");
    readNewData();
}

void FileMonitor::processInotifyEvents() {
    std::vector<char> buffer(kEventBufferSize);
    const ssize_t length = ::read(inotifyFd, buffer.data(), buffer.size());
    if (length <= 0) {
        if (length < 0 && errno != EINTR && errno != EAGAIN) {
            std::cerr << "Error reading inotify events: " << strerror(errno) << std::endl;
        }
        return;
    }

    bool contentChanged = false;
    bool pathChanged = false;

    for (ssize_t i = 0; i + static_cast<ssize_t>(sizeof(struct inotify_event)) <= length;) {
        const auto* event = reinterpret_cast<const struct inotify_event*>(&buffer[i]);

        if (event->wd == fileWatch) {
            if (event->mask & IN_MODIFY) {
                contentChanged = true;
            }
            if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF | IN_IGNORED)) {
                pathChanged = true;
            }
        } else if (event->wd == dirWatch && event->len > 0) {
            if ((event->mask & kDirectoryEvents) && fileName == event->name) {
                pathChanged = true;
            }
        }

        // Always advance, whatever happened above. Skipping this is what turned a failed
        // open into an infinite loop in the original implementation.
        i += static_cast<ssize_t>(sizeof(struct inotify_event)) + event->len;
    }

    if (pathChanged) {
        checkForReplacement();
    }
    if (contentChanged) {
        readNewData();
    }
}

void FileMonitor::monitor() {
    running.store(true);
    emit(" ", "INIT");

    {
        std::ifstream probe(filePath, std::ios::binary);
        emit(" ", probe.is_open() ? "INIT - FILE OPEN" : "ERROR - FILE OPEN");
    }

    // Publish whatever the file already holds, then follow it from there.
    readNewData();

    struct pollfd fds[2];
    fds[0].fd = inotifyFd;
    fds[0].events = POLLIN;
    fds[1].fd = stopFd;
    fds[1].events = POLLIN;

    while (!stopRequested.load()) {
        fds[0].revents = 0;
        fds[1].revents = 0;

        const int ready = poll(fds, 2, kPollTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "poll() failed for '" << filePath << "': " << strerror(errno) << std::endl;
            break;
        }
        if (fds[1].revents & POLLIN) {
            break;  // stop() was called
        }
        if (ready == 0) {
            // Idle tick: catches a replacement we were not notified about.
            checkForReplacement();
            continue;
        }
        if (fds[0].revents & POLLIN) {
            processInotifyEvents();
        }
    }

    emit(" ", "CLOSE");
    sink->flush(kFinalFlushMs);
    running.store(false);
}
