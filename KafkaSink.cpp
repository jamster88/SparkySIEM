#include "KafkaSink.h"

#include <librdkafka/rdkafkacpp.h> // Used for the Kafka producer
#include <atomic>                  // Used for the delivery counters
#include <iostream>                // Used for std::cerr
#include <stdexcept>               // Used for std::runtime_error

namespace {
/// How many times send() retries a full local queue before giving up.
constexpr int kQueueFullRetries = 5;
/// How long each of those retries serves the producer's callbacks for, in ms.
constexpr int kQueueFullPollMs = 100;
/// How long the destructor waits for in-flight messages, in ms.
constexpr int kShutdownFlushMs = 2000;
} // namespace

/**
 * @class KafkaSink::DeliveryReporter
 * @brief Delivery report callback that counts and logs the fate of each message.
 *
 * librdkafka invokes dr_cb() from within poll()/flush() once the broker has
 * acknowledged a message or the producer has given up on it. Without this,
 * produce() returning ERR_NO_ERROR is the only signal available, and that only
 * means "queued locally" - which is how messages went missing silently before.
 */
class KafkaSink::DeliveryReporter : public RdKafka::DeliveryReportCb {
public:
    void dr_cb(RdKafka::Message& message) override {
        if (message.err() == RdKafka::ERR_NO_ERROR) {
            ++successes;
            return;
        }
        ++failures;
        // Log the first few failures in full, then stay quiet: a broker outage
        // would otherwise turn stderr into the bottleneck.
        if (failures <= 10) {
            std::cerr << "Kafka delivery failed (" << message.errstr() << ")";
            if (failures == 10) {
                std::cerr << " - suppressing further delivery failure logs";
            }
            std::cerr << std::endl;
        }
    }

    std::atomic<std::size_t> successes{0};
    std::atomic<std::size_t> failures{0};
};

/**
 * @brief Creates a producer for the given broker and topic.
 */
KafkaSink::KafkaSink(const std::string& broker,
                     const std::string& topic,
                     const std::vector<std::pair<std::string, std::string>>& extraConfig)
    : topic(topic), reporter(new DeliveryReporter()), droppedCount(0) {
    std::string errstr;
    // unique_ptr so an exception on any later line cannot leak the conf object.
    std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

    if (conf->set("bootstrap.servers", broker, errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set Kafka broker: " + errstr);
    }
    if (conf->set("dr_cb", reporter.get(), errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set Kafka delivery report callback: " + errstr);
    }
    for (const auto& kv : extraConfig) {
        if (conf->set(kv.first, kv.second, errstr) != RdKafka::Conf::CONF_OK) {
            throw std::runtime_error("Failed to set Kafka property " + kv.first + ": " + errstr);
        }
    }

    producer.reset(RdKafka::Producer::create(conf.get(), errstr));
    if (!producer) {
        throw std::runtime_error("Failed to create Kafka producer: " + errstr);
    }
}

/**
 * @brief Flushes briefly, then tears down the producer.
 */
KafkaSink::~KafkaSink() {
    if (producer) {
        producer->flush(kShutdownFlushMs);
    }
}

/**
 * @brief Enqueues a message for delivery to the topic.
 */
void KafkaSink::send(const std::string& message) {
    for (int attempt = 0; attempt <= kQueueFullRetries; ++attempt) {
        const RdKafka::ErrorCode resp = producer->produce(
            topic, RdKafka::Topic::PARTITION_UA,
            RdKafka::Producer::RK_MSG_COPY,
            const_cast<char*>(message.c_str()), message.size(),
            nullptr, 0, 0, nullptr, nullptr);

        if (resp == RdKafka::ERR_NO_ERROR) {
            producer->poll(0); // Serve delivery reports without blocking.
            return;
        }

        if (resp == RdKafka::ERR__QUEUE_FULL && attempt < kQueueFullRetries) {
            // Give the producer time to drain rather than spinning or throwing.
            producer->poll(kQueueFullPollMs);
            continue;
        }

        ++droppedCount;
        std::cerr << "Dropping message, failed to enqueue for topic " << topic << ": "
                  << RdKafka::err2str(resp) << std::endl;
        return;
    }
}

/**
 * @brief Waits for outstanding messages to be delivered.
 */
void KafkaSink::flush(int timeoutMs) {
    const RdKafka::ErrorCode resp = producer->flush(timeoutMs);
    if (resp != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Kafka flush did not complete (" << RdKafka::err2str(resp) << "), "
                  << producer->outq_len() << " message(s) still outstanding" << std::endl;
    }
}

std::size_t KafkaSink::deliverySuccesses() const {
    return reporter->successes.load();
}

std::size_t KafkaSink::deliveryFailures() const {
    return reporter->failures.load();
}

std::size_t KafkaSink::dropped() const {
    return droppedCount;
}
