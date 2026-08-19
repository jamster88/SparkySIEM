/**
 * @file KafkaSink.cpp
 * @brief Implementation of the librdkafka-backed MessageSink.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 */

#include "KafkaSink.h"

#include <chrono>     // Used for the queue-full backoff
#include <iostream>   // Used for std::cerr
#include <stdexcept>  // Used for std::runtime_error
#include <thread>     // Used for std::this_thread::sleep_for

namespace {
/// How long to keep retrying when librdkafka's local queue is full.
constexpr int kQueueFullRetries = 50;
/// Delay between those retries, in milliseconds.
constexpr int kQueueFullBackoffMs = 20;
/// Time given to outstanding messages when the sink is destroyed.
constexpr int kShutdownFlushMs = 5000;
}  // namespace

void KafkaSink::DeliveryReporter::dr_cb(RdKafka::Message& message) {
    if (message.err() != RdKafka::ERR_NO_ERROR) {
        ++failures;
        std::cerr << "Kafka delivery failed: " << message.errstr() << std::endl;
    }
}

KafkaSink::KafkaSink(const std::string& kafkaBroker, const std::string& kafkaTopic)
    : topic(kafkaTopic), reporter(std::make_unique<DeliveryReporter>()) {
    std::string errstr;

    // unique_ptr so the configuration is released even if a set() below throws.
    std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    if (!conf) {
        throw std::runtime_error("Failed to create Kafka configuration");
    }
    if (conf->set("bootstrap.servers", kafkaBroker, errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set Kafka broker: " + errstr);
    }
    if (conf->set("dr_cb", reporter.get(), errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set Kafka delivery report callback: " + errstr);
    }

    producer.reset(RdKafka::Producer::create(conf.get(), errstr));
    if (!producer) {
        throw std::runtime_error("Failed to create Kafka producer: " + errstr);
    }
}

KafkaSink::~KafkaSink() {
    if (producer) {
        // Give queued messages a chance to reach the broker before we tear down.
        producer->flush(kShutdownFlushMs);
    }
}

void KafkaSink::send(const std::string& message) {
    for (int attempt = 0; attempt <= kQueueFullRetries; ++attempt) {
        const RdKafka::ErrorCode resp = producer->produce(
            topic, RdKafka::Topic::PARTITION_UA,
            RdKafka::Producer::RK_MSG_COPY,
            const_cast<char*>(message.c_str()), message.size(),
            nullptr, 0, 0, nullptr, nullptr);

        if (resp == RdKafka::ERR_NO_ERROR) {
            producer->poll(0);  // serve delivery reports
            return;
        }
        if (resp != RdKafka::ERR__QUEUE_FULL) {
            throw std::runtime_error("Failed to produce message: " + RdKafka::err2str(resp));
        }
        // Local queue is full: let the producer drain, then try again.
        producer->poll(kQueueFullBackoffMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Failed to produce message: local queue full after retries");
}

void KafkaSink::flush(int timeoutMs) {
    if (producer) {
        producer->flush(timeoutMs);
    }
}

std::size_t KafkaSink::failedDeliveries() const {
    return reporter->failures.load();
}
