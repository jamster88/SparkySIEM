#include "KafkaSink.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Config = std::vector<std::pair<std::string, std::string>>;

/// A broker address with nothing listening behind it.
const char* const kDeadBroker = "127.0.0.1:59998";

/**
 * @brief Builds a KafkaSink backed by librdkafka's in-process mock cluster.
 * @return The sink, or nullptr if this librdkafka build has no mock support.
 */
std::unique_ptr<KafkaSink> makeMockSink(const std::string& topic, Config extra = {}) {
    extra.emplace_back("test.mock.num.brokers", "1");
    try {
        return std::unique_ptr<KafkaSink>(new KafkaSink("localhost:9092", topic, extra));
    } catch (const std::exception&) {
        return nullptr;
    }
}

} // namespace

TEST(KafkaSinkDelivery, MessagesReachTheBrokerAndAreCountedAsDelivered) {
    std::unique_ptr<KafkaSink> sink = makeMockSink("unit-test-topic");
    if (!sink) {
        GTEST_SKIP() << "this librdkafka build has no mock cluster support";
    }

    constexpr int kMessages = 5;
    for (int i = 0; i < kMessages; ++i) {
        sink->send("{\"n\": \"" + std::to_string(i) + "\"}");
    }
    sink->flush(15000);

    EXPECT_EQ(sink->deliverySuccesses(), static_cast<std::size_t>(kMessages));
    EXPECT_EQ(sink->deliveryFailures(), 0u);
    EXPECT_EQ(sink->dropped(), 0u);
}

TEST(KafkaSinkDelivery, SuccessIsOnlyCountedAfterTheBrokerAcknowledges) {
    std::unique_ptr<KafkaSink> sink = makeMockSink("unit-test-topic");
    if (!sink) {
        GTEST_SKIP() << "this librdkafka build has no mock cluster support";
    }

    // produce() only enqueues, so nothing is confirmed delivered until the
    // producer has been served - this is what made losses invisible before.
    sink->send("{\"n\": \"0\"}");
    EXPECT_EQ(sink->deliverySuccesses(), 0u);

    sink->flush(15000);
    EXPECT_EQ(sink->deliverySuccesses(), 1u);
}

TEST(KafkaSinkFailures, AnUnreachableBrokerIsReportedRatherThanIgnored) {
    // A short message timeout makes librdkafka give up quickly and hand each
    // message to the delivery report callback as a failure.
    KafkaSink sink(kDeadBroker, "unit-test-topic", {{"message.timeout.ms", "1500"}});

    constexpr int kMessages = 3;
    for (int i = 0; i < kMessages; ++i) {
        sink.send("{\"n\": \"" + std::to_string(i) + "\"}");
    }
    // produce() reported no error even though the broker does not exist.
    EXPECT_EQ(sink.dropped(), 0u);

    sink.flush(15000);

    EXPECT_EQ(sink.deliveryFailures(), static_cast<std::size_t>(kMessages));
    EXPECT_EQ(sink.deliverySuccesses(), 0u);
}

TEST(KafkaSinkFailures, AFullQueueIsDroppedAndCountedInsteadOfThrowing) {
    // One message of headroom and a timeout long enough that the queue stays
    // full: every later send() has nowhere to put its message.
    KafkaSink sink(kDeadBroker, "unit-test-topic",
                   {{"queue.buffering.max.messages", "1"},
                    {"message.timeout.ms", "30000"}});

    // Throwing from here is what previously killed the process outright.
    ASSERT_NO_THROW({
        for (int i = 0; i < 4; ++i) {
            sink.send("{\"n\": \"" + std::to_string(i) + "\"}");
        }
    });

    EXPECT_GE(sink.dropped(), 1u);
}

TEST(KafkaSinkConstruction, RejectsAnUnknownConfigurationProperty) {
    EXPECT_THROW(KafkaSink("localhost:9092", "t", {{"not.a.real.property", "1"}}),
                 std::runtime_error);
}

TEST(KafkaSinkConstruction, AcceptsADeadBrokerBecauseConnectionIsAsynchronous) {
    // Documenting real librdkafka behaviour: construction cannot tell you the
    // broker is unusable, which is precisely why delivery reports are needed.
    EXPECT_NO_THROW(KafkaSink(kDeadBroker, "unit-test-topic"));
}

TEST(KafkaSinkFlush, IsSafeWithNothingOutstanding) {
    std::unique_ptr<KafkaSink> sink = makeMockSink("unit-test-topic");
    if (!sink) {
        GTEST_SKIP() << "this librdkafka build has no mock cluster support";
    }
    EXPECT_NO_THROW(sink->flush(1000));
    EXPECT_EQ(sink->deliverySuccesses(), 0u);
}
