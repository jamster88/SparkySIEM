/**
 * @file kafka_mock.h
 * @brief Mock Kafka Producer for unit testing without a real broker.
 *
 * Provides a lightweight mock that records produce() calls so tests can verify
 * what messages were sent, without requiring librdkafka or a running Kafka instance.
 */

#ifndef KAFKA_MOCK_H
#define KAFKA_MOCK_H

#include <string>
#include <vector>
#include <cstdint>

/**
 * @brief Record of a single produce() call made through the mock.
 */
struct ProduceRecord {
    std::string topic;       ///< Kafka topic name
    int32_t partition;       ///< Partition number (-1 = any)
    int flags;               ///< Message flags (RK_MSG_COPY etc.)
    std::string payload;     ///< Message content
    size_t payload_len;      ///< Length of the payload
};

/**
 * @brief Mock Kafka Producer for unit testing.
 *
 * Simulates librdkafka's Producer class by recording all produce() calls.
 * Does not require librdkafka to be linked.
 */
class MockKafkaProducer {
public:
    MockKafkaProducer() = default;

    /**
     * @brief Produces a message (mock implementation).
     * Records the call and returns ERR_NO_ERROR (0) to simulate success.
     */
    int produce(const std::string& topic, int32_t partition, int flags,
                const char* payload, size_t payload_len,
                void* key = nullptr, void** opaque = nullptr,
                void* rkbnd = nullptr, void* msgfn = nullptr) {
        ProduceRecord record;
        record.topic = topic;
        record.partition = partition;
        record.flags = flags;
        record.payload = std::string(payload, payload_len);
        record.payload_len = payload_len;
        records_.push_back(record);
        return 0; // ERR_NO_ERROR
    }

    /**
     * @brief Polls the producer (mock - no-op).
     */
    int poll(int timeout_ms) { return 0; }

    /**
     * @brief Flushes pending messages (mock - no-op).
     */
    void flush(int timeout_ms) {}

    /**
     * @brief Returns all recorded produce() calls.
     */
    const std::vector<ProduceRecord>& getRecords() const { return records_; }

    /**
     * @brief Clears all recorded produce() calls.
     */
    void clear() { records_.clear(); }

    /**
     * @brief Returns the number of produce() calls made.
     */
    size_t getRecordCount() const { return records_.size(); }

private:
    std::vector<ProduceRecord> records_;
};

/**
 * @brief Mock librdkafka namespace for testing without linking librdkafka.
 */
namespace mock_rdkafka {

/**
 * @brief Error code enum (matches RdKafka::ERR_NO_ERROR = 0).
 */
enum class ErrorCode {
    ERR_NO_ERROR = 0,
    ERR_TOPIC_RESOLUTION_FAILED = 169,
    ERR_QUEUE_FULL = 132,
};

/**
 * @brief Topic namespace mock.
 */
namespace Topic {
    constexpr int PARTITION_UA = -1;
}

} // namespace mock_rdkafka

#endif
