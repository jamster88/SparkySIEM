#ifndef KAFKASINK_H
#define KAFKASINK_H

#include "MessageSink.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace RdKafka {
class Producer;
class Conf;
}

/**
 * @class KafkaSink
 * @brief MessageSink that produces messages to a Kafka topic via librdkafka.
 *
 * Delivery in librdkafka is asynchronous: produce() only enqueues, so a
 * successful return says nothing about whether the broker ever got the message.
 * This class therefore registers a delivery report callback and counts the
 * outcomes, so that lost messages are visible through deliveryFailures() and on
 * stderr rather than disappearing silently.
 */
class KafkaSink : public MessageSink {
public:
    /**
     * @brief Creates a producer for the given broker and topic.
     * @param broker The bootstrap broker address, e.g. "localhost:9092".
     * @param topic The topic messages are produced to.
     * @param extraConfig Optional additional librdkafka properties, applied
     *        after the defaults. Used by the tests to spin up an in-process
     *        mock cluster via "test.mock.num.brokers".
     * @throws std::runtime_error If the producer cannot be configured or created.
     */
    KafkaSink(const std::string& broker,
              const std::string& topic,
              const std::vector<std::pair<std::string, std::string>>& extraConfig = {});

    /**
     * @brief Flushes briefly, then tears down the producer.
     */
    ~KafkaSink() override;

    /**
     * @brief Enqueues a message for delivery to the topic.
     *
     * Retries a bounded number of times if the local queue is full, then counts
     * the message as dropped and logs to stderr. Never throws, so a transport
     * problem cannot take down the monitor loop.
     */
    void send(const std::string& message) override;

    /**
     * @brief Waits for outstanding messages to be delivered.
     * @param timeoutMs Maximum time to wait, in milliseconds.
     */
    void flush(int timeoutMs) override;

    /** @brief Number of messages the broker has acknowledged. */
    std::size_t deliverySuccesses() const;

    /** @brief Number of messages the broker rejected or that timed out. */
    std::size_t deliveryFailures() const;

    /** @brief Number of messages that could not be enqueued locally at all. */
    std::size_t dropped() const;

private:
    class DeliveryReporter;

    std::string topic;                            ///< Destination topic.
    std::unique_ptr<DeliveryReporter> reporter;   ///< Delivery report callback and counters.
    std::unique_ptr<RdKafka::Producer> producer;  ///< The librdkafka producer.
    std::size_t droppedCount;                     ///< Messages never enqueued.
};

#endif
