#ifndef TESTSUPPORT_H
#define TESTSUPPORT_H

#include "FileMonitor.h"
#include "MessageSink.h"

#include <dirent.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace testsupport {

/**
 * @class RecordingSink
 * @brief MessageSink that keeps every message so tests can assert on them.
 *
 * The monitor loop runs on its own thread, so access is mutex guarded and
 * waitForCount() lets a test block until the expected traffic has arrived
 * instead of sleeping for a fixed period.
 */
class RecordingSink : public MessageSink {
public:
    void send(const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex);
        messages.push_back(message);
        condition.notify_all();
    }

    void flush(int /*timeoutMs*/) override {
        std::lock_guard<std::mutex> lock(mutex);
        ++flushes;
        condition.notify_all();
    }

    /** @brief Blocks until at least n messages have arrived, or the timeout expires. */
    bool waitForCount(std::size_t n,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [&] { return messages.size() >= n; });
    }

    /** @brief Blocks until at least n messages of the given type have arrived. */
    bool waitForType(const std::string& type, std::size_t n = 1,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [&] { return countTypeLocked(type) >= n; });
    }

    std::vector<std::string> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        return messages;
    }

    std::size_t count() const {
        std::lock_guard<std::mutex> lock(mutex);
        return messages.size();
    }

    std::size_t flushCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return flushes;
    }

private:
    std::size_t countTypeLocked(const std::string& type) const;

    mutable std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> messages;
    std::size_t flushes = 0;
};

// ---------------------------------------------------------------------------
// Minimal JSON reading, so tests verify that a message really does decode back
// to the original content rather than just eyeballing the escape sequences.
// ---------------------------------------------------------------------------

/**
 * @brief Extracts and unescapes a string field from a flat JSON object.
 * @param json The message to read.
 * @param key The field name.
 * @param out Receives the decoded value.
 * @return True if the field was found and decoded, false otherwise.
 */
bool jsonField(const std::string& json, const std::string& key, std::string& out);

/** @brief Convenience wrapper around jsonField() that returns "" when absent. */
std::string field(const std::string& json, const std::string& key);

/**
 * @brief Checks that a message is a flat JSON object of quoted string fields.
 *
 * Deliberately strict and simple: it walks the object, requires every key and
 * value to be a correctly terminated and correctly escaped JSON string, and
 * rejects anything else. That is enough to catch unescaped content, which is how
 * invalid messages were produced before.
 */
bool isWellFormedFlatJsonObject(const std::string& json);

/** @brief Returns just the messages whose "type" field equals type. */
std::vector<std::string> ofType(const std::vector<std::string>& messages,
                                const std::string& type);

/** @brief Returns the "message" field of every message whose type equals type. */
std::vector<std::string> linesOfType(const std::vector<std::string>& messages,
                                     const std::string& type);

// ---------------------------------------------------------------------------
// Filesystem and monitor lifecycle helpers
// ---------------------------------------------------------------------------

/**
 * @class TempDir
 * @brief Creates a unique temporary directory and removes it on destruction.
 */
class TempDir {
public:
    TempDir();
    ~TempDir();

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /** @brief Absolute path of the directory. */
    const std::string& path() const { return dir; }

    /** @brief Absolute path of a file inside the directory. */
    std::string file(const std::string& name) const { return dir + "/" + name; }

private:
    std::string dir;
};

/** @brief Overwrites a file with the given contents. */
void writeFile(const std::string& path, const std::string& contents);

/** @brief Appends to a file and flushes so the write reaches the filesystem. */
void appendToFile(const std::string& path, const std::string& contents);

/** @brief Reads a whole file into a string. */
std::string readFile(const std::string& path);

/**
 * @class MonitorHarness
 * @brief Runs a FileMonitor on a background thread with a RecordingSink attached.
 *
 * Construction sets up the monitor (which snapshots the file's size), start()
 * begins the loop, and stopAndJoin() shuts it down. The destructor stops the
 * thread too, so a failing assertion cannot leave a monitor running.
 */
class MonitorHarness {
public:
    explicit MonitorHarness(const std::string& filePath,
                            const std::string& topic = "test-topic");
    ~MonitorHarness();

    MonitorHarness(const MonitorHarness&) = delete;
    MonitorHarness& operator=(const MonitorHarness&) = delete;

    /** @brief Starts the monitor thread and waits for its INIT messages. */
    void start();

    /** @brief Stops the monitor and joins its thread. Idempotent. */
    void stopAndJoin();

    RecordingSink& sink() { return *recordingSink; }
    FileMonitor& monitor() { return *fileMonitor; }

    /** @brief All messages seen so far. */
    std::vector<std::string> messages() const { return recordingSink->snapshot(); }

    /** @brief The "message" field of every message of the given type, in order. */
    std::vector<std::string> lines(const std::string& type = "MODIFY") const {
        return linesOfType(recordingSink->snapshot(), type);
    }

private:
    RecordingSink* recordingSink;
    std::unique_ptr<FileMonitor> fileMonitor;
    std::thread thread;
    bool joined = false;
};

/**
 * @brief Waits for the message count to stop changing, then returns it.
 *
 * Used to assert that a pathological input does not cause the monitor to keep
 * producing messages forever.
 */
std::size_t settleAndCount(RecordingSink& sink,
                           std::chrono::milliseconds quietPeriod = std::chrono::milliseconds(700));

} // namespace testsupport

#endif
