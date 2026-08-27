/**
 * @file TestSupport.h
 * @brief Shared helpers for the SparkySIEM unit tests.
 *
 * Provides a scratch directory that cleans itself up, small file-writing helpers, a
 * polling wait so the tests do not depend on fixed sleeps, a runner that drives a
 * blocking FileMonitor on its own thread, and JSON accessors for asserting on the
 * messages a monitor published.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 */

#ifndef TESTS_TESTSUPPORT_H
#define TESTS_TESTSUPPORT_H

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <cstdlib>
#include <nlohmann/json.hpp>

#include "FileMonitor.h"

namespace sparky::testing {

namespace fs = std::filesystem;

/// How long the helpers wait for an asynchronous event before giving up.
inline constexpr std::chrono::milliseconds kDefaultTimeout{5000};
/// Gap between polls while waiting.
inline constexpr std::chrono::milliseconds kPollGap{10};

/**
 * @brief A unique scratch directory removed when the object goes out of scope.
 */
class TempDir {
public:
    TempDir() {
        std::string pattern = (fs::temp_directory_path() / "sparky_test_XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        if (mkdtemp(buffer.data()) == nullptr) {
            throw std::runtime_error("mkdtemp failed for pattern " + pattern);
        }
        root = fs::path(buffer.data());
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /// The directory itself.
    const fs::path& path() const { return root; }

    /// A path inside the directory. The file is not created.
    std::string file(const std::string& name) const { return (root / name).string(); }

private:
    fs::path root;
};

/// Writes @p content to @p path, replacing anything already there.
inline void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot write " + path);
    }
    out << content;
}

/// Appends @p content to @p path and closes the file so inotify reports the change.
inline void appendToFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        throw std::runtime_error("cannot append to " + path);
    }
    out << content;
    out.flush();
}

/**
 * @brief Polls @p predicate until it is true or the timeout expires.
 * @return The final value of the predicate.
 */
template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = kDefaultTimeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(kPollGap);
    }
    return predicate();
}

/**
 * @brief Runs a FileMonitor on a background thread and shuts it down on destruction.
 *
 * FileMonitor::monitor() blocks until stop(), so every test that exercises the loop
 * needs it on a thread of its own.
 */
class MonitorRunner {
public:
    explicit MonitorRunner(std::unique_ptr<FileMonitor> monitorToRun)
        : monitor(std::move(monitorToRun)) {
        worker = std::thread([this] { monitor->monitor(); });
        // Do not race the test against startup: wait until the loop is live.
        waitFor([this] { return monitor->isRunning(); });
    }

    ~MonitorRunner() { stopAndJoin(); }

    MonitorRunner(const MonitorRunner&) = delete;
    MonitorRunner& operator=(const MonitorRunner&) = delete;

    /// Stops the monitor and joins its thread. Safe to call more than once.
    void stopAndJoin() {
        if (worker.joinable()) {
            monitor->stop();
            worker.join();
        }
    }

    FileMonitor& get() { return *monitor; }

private:
    std::unique_ptr<FileMonitor> monitor;
    std::thread worker;
};

/// Parses one published message. Throws if the message is not valid JSON.
inline nlohmann::json parseMessage(const std::string& message) {
    return nlohmann::json::parse(message);
}

/// The "type" field of every message, in order.
inline std::vector<std::string> typesOf(const std::vector<std::string>& messages) {
    std::vector<std::string> types;
    types.reserve(messages.size());
    for (const auto& message : messages) {
        types.push_back(parseMessage(message).at("type").get<std::string>());
    }
    return types;
}

/// Every message whose "filePath" equals @p path, in order. Lets the FilesMonitor tests
/// pick one file's traffic out of a sink that several monitors share.
inline std::vector<std::string> messagesFor(const std::vector<std::string>& messages,
                                            const std::string& path) {
    std::vector<std::string> forPath;
    for (const auto& message : messages) {
        if (parseMessage(message).at("filePath").get<std::string>() == path) {
            forPath.push_back(message);
        }
    }
    return forPath;
}

/// The "message" field of every message whose "type" equals @p type, in order.
inline std::vector<std::string> bodiesOfType(const std::vector<std::string>& messages,
                                             const std::string& type) {
    std::vector<std::string> bodies;
    for (const auto& message : messages) {
        const auto parsed = parseMessage(message);
        if (parsed.at("type").get<std::string>() == type) {
            bodies.push_back(parsed.at("message").get<std::string>());
        }
    }
    return bodies;
}

/// The content lines a monitor published, i.e. the bodies of its "MODIFY" messages.
inline std::vector<std::string> contentLines(const std::vector<std::string>& messages) {
    return bodiesOfType(messages, "MODIFY");
}

/// How many messages carry the given type.
inline std::size_t countOfType(const std::vector<std::string>& messages, const std::string& type) {
    std::size_t count = 0;
    for (const auto& message : messages) {
        if (parseMessage(message).at("type").get<std::string>() == type) {
            ++count;
        }
    }
    return count;
}

/// How many times @p line appears among the published content lines.
inline std::size_t countLine(const std::vector<std::string>& messages, const std::string& line) {
    std::size_t count = 0;
    for (const auto& body : contentLines(messages)) {
        if (body == line) {
            ++count;
        }
    }
    return count;
}

}  // namespace sparky::testing

#endif
