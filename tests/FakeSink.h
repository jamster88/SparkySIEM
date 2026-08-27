/**
 * @file FakeSink.h
 * @brief In-memory MessageSink implementations used by the unit tests.
 *
 * FakeSink records everything published so a test can assert on it without a broker.
 * ThrowingSink fails every send, which is how the tests check that a publish failure
 * does not take a monitor down. SlowFlushSink makes the cost of draining a sink on
 * shutdown visible, so a test can prove no lock is held across it.
 *
 * All three are thread safe: a FilesMonitor shares one sink across several monitor
 * threads.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 */

#ifndef TESTS_FAKESINK_H
#define TESTS_FAKESINK_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "MessageSink.h"

namespace sparky::testing {

/**
 * @brief Records published messages in memory.
 */
class FakeSink : public MessageSink {
public:
    void send(const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex);
        recorded.push_back(message);
    }

    void flush(int /*timeoutMs*/) override { ++flushCalls; }

    /// A snapshot of everything published so far.
    std::vector<std::string> messages() const {
        std::lock_guard<std::mutex> lock(mutex);
        return recorded;
    }

    /// How many messages have been published.
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return recorded.size();
    }

    /// How many times flush() was called.
    std::size_t flushes() const { return flushCalls.load(); }

    /// Forgets everything recorded so far.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        recorded.clear();
    }

private:
    mutable std::mutex mutex;
    std::vector<std::string> recorded;
    std::atomic<std::size_t> flushCalls{0};
};

/**
 * @brief A FakeSink whose flush() takes a noticeable amount of time.
 *
 * A real KafkaSink spends up to a second draining when a monitor shuts down. This makes
 * that cost measurable, so a test can show the scanning thread does not hold the monitor
 * lock while a deleted file's monitor winds down.
 */
class SlowFlushSink : public FakeSink {
public:
    explicit SlowFlushSink(std::chrono::milliseconds flushDelay) : delay(flushDelay) {}

    void flush(int timeoutMs) override {
        std::this_thread::sleep_for(delay);
        FakeSink::flush(timeoutMs);
    }

private:
    std::chrono::milliseconds delay;
};

/**
 * @brief Fails every publish, and counts the attempts.
 */
class ThrowingSink : public MessageSink {
public:
    void send(const std::string& /*message*/) override {
        ++attemptCount;
        throw std::runtime_error("sink is unavailable");
    }

    void flush(int /*timeoutMs*/) override {}

    /// How many publishes were attempted.
    std::size_t attempts() const { return attemptCount.load(); }

private:
    std::atomic<std::size_t> attemptCount{0};
};

}  // namespace sparky::testing

#endif
