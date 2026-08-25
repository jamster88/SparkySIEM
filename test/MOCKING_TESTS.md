# Mocking Tests for FileMonitor

## Overview

The FileMonitor class uses Linux-specific `inotify` API for file monitoring. To test this on non-Linux platforms (like macOS), we use mock-based testing.

## Building Tests

```bash
# Build gtest (if not already built)
cd /tmp/googletest-1.14.0
mkdir -p build && cd build
cmake .. && make

# Compile tests
g++ -std=c++17 \
    -I/tmp/googletest-1.14.0/googletest/include \
    -I/opt/homebrew/opt/librdkafka/include \
    -I/Users/jamster/Repos/SparkySIEM \
    -I/Users/jamster/Repos/SparkySIEM/test \
    /Users/jamster/Repos/SparkySIEM/test/*.cpp \
    /tmp/googletest-1.14.0/build/lib/libgtest.a \
    -lpthread \
    -o tests

# Run tests
./tests
```

## Mocking Strategy

### 1. inotify Mocking

Since inotify is Linux-specific, we can mock the inotify system calls:

```cpp
class MockInotify {
public:
    MOCK_METHOD(int, inotify_init, (), ());
    MOCK_METHOD(int, inotify_add_watch, (int fd, const char* path, uint32_t mask), ());
    MOCK_METHOD(int, inotify_rm_watch, (int fd, int wd), ());
    MOCK_METHOD(ssize_t, read, (int fd, void* buf, size_t count), ());
    MOCK_METHOD(int, close, (int fd), ());
};
```

### 2. Kafka Producer Mocking

Similarly, mock the Kafka producer for unit tests:

```cpp
class MockKafkaProducer {
public:
    MOCK_METHOD(int, produce, (const char* topic, const char* message), ());
    MOCK_METHOD(void, flush, (int timeout_ms), ());
    MOCK_METHOD(void, poll, (int timeout_ms), ());
};
```

## Testing inotify Event Processing

To test the inotify event loop, we can create a test that simulates events:

```cpp
TEST(FileMonitorTest, InotifyEventProcessing) {
    // Simulate IN_MODIFY event
    struct inotify_event event = {
        .wd = 1,
        .mask = IN_MODIFY,
        .cookie = 0,
        .len = 0
    };

    // Test that the event mask is correctly detected
    EXPECT_TRUE((event.mask & IN_MODIFY) != 0);
}
```

## Integration Testing on Linux

For full integration testing with actual inotify and Kafka:

```bash
# Start Kafka
cd docker_stuff
docker compose up -d

# Create topic
docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh \
    --create --topic my-topic --bootstrap-server localhost:9092

# Run the file monitor (on Linux)
./sparky_siem /path/to/file localhost:9092 my-topic

# In another terminal, watch Kafka messages
docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic my-topic --from-beginning --bootstrap-server localhost:9092

# Modify the file to trigger events
echo "Test message" >> /path/to/file
```

## CI/CD Considerations

For CI/CD pipelines:
1. Use a Linux runner (GitHub Actions: `ubuntu-latest`)
2. Install dependencies: `librdkafka-dev`, `gtest`
3. Start Kafka in a container before tests
4. Run tests that verify file monitoring and Kafka messaging

## Limitations

- on macOS (no inotify)
- Kafka integration requires network connectivity
- Full end-to-end testing requires Linux

## Recommended Testing Strategy

1. **Unit Tests** (run everywhere): Test timestamp generation, message formatting, string operations
2. **Mock Tests** (Linux only): Test inotify event processing with mocked system calls
3. **Integration Tests** (Linux + Kafka): Test full file monitoring with real Kafka
