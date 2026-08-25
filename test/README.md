# File Change Monitor Tests

This directory contains unit tests for the SparkySIEM file change monitor.

## Test Files

- `standalone_tests.cpp` - Standalone tests for FileMonitor logic (timestamp, message formatting, file operations)
- `standalone_filesmonitor_tests.cpp` - Standalone tests for FilesMonitor logic (path management, map operations, thread safety)
- `MOCKING_TESTS.md` - Documentation on mocking strategies for inotify and Kafka

## Test Summary

### File Monitor Tests (standalone_tests.cpp)
- Timestamp formatting and uniqueness
- Message formatting with JSON structure
- File system operations (create, read, append, delete)
- Thread safety tests
- JSON parsing validation

**Total: 18 tests**

### Files Monitor Tests (standalone_filesmonitor_tests.cpp)
- Path manipulation and directory iteration
- Multiple path tracking
- File creation, deletion, and modification detection
- Map management (add, remove, duplicate prevention)
- Cleanup logic for deleted files
- Thread-safe path addition
- HandleFile logic (new/existing files)
- Integration-style tests

**Total: 25 tests**

## Running Tests on macOS (Current)

```bash
# Build gtest (if not already built)
cd /tmp/googletest-1.14.0
mkdir -p build && cd build
cmake .. && make

# Run File Monitor tests
cd /tmp
g++ -std=c++17 \
    -I/tmp/googletest-1.14.0/googletest/include \
    -I/tmp/googletest-1.14.0/googletest \
    -I/Users/jamster/Repos/SparkySIEM/test \
    /Users/jamster/Repos/SparkySIEM/test/standalone_tests.cpp \
    /tmp/googletest-1.14.0/build/lib/libgtest.a \
    -lpthread \
    -o standalone_tests

./standalone_tests

# Run Files Monitor tests
g++ -std=c++17 \
    -I/tmp/googletest-1.14.0/googletest/include \
    -I/tmp/googletest-1.14.0/googletest \
    -I/Users/jamster/Repos/SparkySIEM/test \
    /Users/jamster/Repos/SparkySIEM/test/standalone_filesmonitor_tests.cpp \
    /tmp/googletest-1.14.0/build/lib/libgtest.a \
    -lpthread \
    -o filesmonitor_tests

./filesmonitor_tests
```

## Running Tests on Linux (with inotify)

On Linux, you can also test the inotify-based monitoring:

```bash
# Install dependencies
sudo apt-get install libgtest-dev librdkafka-dev g++

# Build gtest
cd /usr/src/googletest
sudo cmake . && sudo make
sudo cp googlemock/gtest/*.a /usr/lib

# Build tests
cd /path/to/SparkySIEM/test
g++ -std=c++17 \
    -I/usr/include \
    -I/opt/homebrew/opt/librdkafka/include \
    FileMonitor_test.cpp \
    /usr/lib/libgtest.a \
    -lrdkafka \
    -lpthread \
    -o file_monitor_linux_tests

# Run tests
./file_monitor_linux_tests
```

## Integration Testing with Kafka

To run full integration tests with Kafka:

```bash
# Start Kafka (on Linux)
cd /path/to/SparkySIEM/docker_stuff
docker compose up -d

# Create topic
docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh \
    --create --topic my-topic --bootstrap-server localhost:9092

# Build and run the monitor (on Linux)
cd /path/to/SparkySIEM
g++ -std=c++17 -rdkafka FileMonitor.cpp main.cpp -o sparky_siem

# Start the monitor in one terminal
./sparky_siem /path/to/test/file localhost:9092 my-topic

# In another terminal, watch Kafka messages
docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic my-topic --from-beginning --bootstrap-server localhost:9092

# Modify the test file to trigger events
echo "Test message" >> /path/to/test/file
```

## CI/CD Configuration

For GitHub Actions or other CI/CD:

```yaml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    services:
      kafka:
        image: apache/kafka:latest
        ports:
          - 9092:9092
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup GTest
        run: |
          sudo apt-get install libgtest-dev
          cd /usr/src/googletest
          sudo cmake .
          sudo make
          sudo cp googlemock/gtest/*.a /usr/lib
      
      - name: Install librdkafka
        run: |
          sudo apt-get install librdkafka-dev
      
      - name: Create Kafka topic
        run: |
          docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh \
            --create --topic my-topic --bootstrap-server localhost:9092
      
      - name: Build tests
        run: g++ -std=c++17 -I/usr/include test/*.cpp /usr/lib/libgtest.a -lrdkafka -lpthread -o tests
      
      - name: Run tests
        run: ./tests
```

## Test Coverage

### Unit Tests (Running on all platforms)
- [x] Timestamp formatting
- [x] Message formatting (JSON)
- [x] File operations (create, read, write)
- [x] Directory iteration
- [x] Path management
- [x] Thread safety
- [x] Map operations
- [x] Cleanup logic

### Integration Tests (Linux only)
- [ ] inotify event detection
- [ ] Kafka message production
- [ ] End-to-end file monitoring

## Notes

- The FileMonitor class uses Linux-specific `inotify` API
- To test on macOS, use mock-based testing or run tests in a Linux environment
- Kafka integration requires network connectivity to a running Kafka broker
- All tests are designed to be platform-independent where possible
