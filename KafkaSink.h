/**
 * @file KafkaSink.h
 * @brief MessageSink implementation backed by librdkafka.
 *
 * Owns a single RdKafka::Producer and publishes every message to one topic. A
 * delivery report callback is installed so that failures the broker reports
 * asynchronously are surfaced instead of being silently dropped.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 */

#ifndef KAFKASINK_H
#define KAFKASINK_H

#include <atomic>
#include <memory>
#include <string>

#include <librdkafka/rdkafkacpp.h>

#include "MessageSink.h"

/**
 * @brief Publishes messages to a Kafka topic.
 */
class KafkaSink : public MessageSink {
public:
    /**
     * @brief Creates a producer connected to the given broker.
     * @param kafkaBroker The bootstrap server address, e.g. "localhost:9092".
     * @param kafkaTopic  The topic every message is published to.
     * @throws std::runtime_error If the producer could not be configured or created.
     */
    KafkaSink(const std::string& kafkaBroker, const std::string& kafkaTopic);

    /**
     * @brief Flushes outstanding messages and tears the producer down.
     */
    ~KafkaSink() override;

    KafkaSink(const KafkaSink&) = delete;
    KafkaSink& operator=(const KafkaSink&) = delete;

    /**
     * @brief Enqueues a message for delivery to the topic.
     *
     * A full local queue is retried briefly rather than treated as a hard failure.
     *
     * @param message The message to publish.
     * @throws std::runtime_error If the message could not be enqueued.
     */
    void send(const std::string& message) override;

    /**
     * @brief Waits for in-flight messages to be acknowledged.
     * @param timeoutMs Maximum time to wait, in milliseconds.
     */
    void flush(int timeoutMs) override;

    /**
     * @brief Number of messages the broker failed to accept.
     * @return The count reported by the delivery report callback.
     */
    std::size_t failedDeliveries() const;

private:
    /// Logs and counts messages the broker rejected.
    class DeliveryReporter : public RdKafka::DeliveryReportCb {
    public:
        void dr_cb(RdKafka::Message& message) override;
        std::atomic<std::size_t> failures{0};
    };

    std::string topic;                          ///< Destination topic.
    std::unique_ptr<DeliveryReporter> reporter; ///< Declared first so it outlives the producer.
    std::unique_ptr<RdKafka::Producer> producer;///< Destroyed before the reporter it references.
};

#endif
