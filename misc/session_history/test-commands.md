# Test Commands for SparkySIEM

## Running Unit Tests

### Direct Execution

```bash
# Run FileMonitor tests
./test/filemonitor_tests

# Run FilesMonitor tests
./test/filesmonitor_tests

# Run all tests with verbose output
./test/filemonitor_tests --gtest_filter=*.*
./test/filesmonitor_tests --gtest_filter=*.*

# Run specific test suites
./test/filemonitor_tests --gtest_filter=TimestampTests.*
./test/filemonitor_tests --gtest_filter=MessageFormatTests.*
./test/filesmonitor_tests --gtest_filter=FilesMonitorPathTest.*

# Run tests with death tests
./test/filemonitor_tests --gtest_filter=*DeathTest*

# Run tests with parallel execution
./test/filemonitor_tests --gtest_parallel
```

### With GTest Options

```bash
# List all available tests
./test/filemonitor_tests --gtest_list_tests

# Set random seed for test ordering
./test/filemonitor_tests --gtest_shuffle --gtest_random_seed=12345

# Repeat tests
./test/filemonitor_tests --gtest_repeat=10

# Break on failure
./test/filemonitor_tests --gtest_break_on_failure

# Filter tests
./test/filemonitor_tests --gtest_filter="-ThreadTests.*"

# Output JUnit XML
./test/filemonitor_tests --gtest_output=xml:results.xml
```

### Running with Valgrind (Memory Testing)

```bash
# Check for memory leaks
valgrind --leak-check=full ./test/filemonitor_tests
valgrind --leak-check=full ./test/filesmonitor_tests

# Check for memory errors
valgrind --tool=memcheck ./test/filemonitor_tests
```

### Running with GDB (Debugging)

```bash
# Run under GDB
gdb ./test/filemonitor_tests
(gdb) run
(gdb) bt  # backtrace on failure

# Run specific test under GDB
gdb ./test/filemonitor_tests
(gdb) run --gtest_filter=TimestampTests.FormatCorrect
```

## Running Tests in Docker

### Build and Run

```bash
# Build test image
docker build -t sparky_siem_test:latest -f docker_stuff/Dockerfile.test ..

# Run tests in container
docker run --rm sparky_siem_test:latest

# Run with interactive shell
docker run -it sparky_siem_test:latest /bin/bash

# Inside container, run tests
/app/test/filemonitor_tests
/app/test/filesmonitor_tests
```

### Run with Docker Compose

```bash
cd /Users/jamster/Repos/SparkySIEM/docker_stuff

# Run tests with Kafka
docker compose run --rm test-runner

# Run tests with verbose output
docker compose run --rm test-runner /app/test/filemonitor_tests --gtest_list_tests
```

## Integration Testing

### With Kafka

```bash
# Start Kafka
docker compose up -d kafka

# Wait for Kafka to be ready
sleep 45

# Run tests that require Kafka
./test/filemonitor_tests --gtest_filter=Kafka*

# Stop Kafka
docker compose down
```

### Manual Integration Test

```bash
# Terminal 1: Start file monitor
./sparky_siem /path/to/test/file localhost:9092 my-topic

# Terminal 2: Watch Kafka messages
docker exec ctest_broker kafka-console-consumer.sh \
    --topic my-topic --from-beginning --bootstrap-server localhost:9092

# Terminal 3: Modify test file
echo "Test message" >> /path/to/test/file
```

## Test Output

### Expected Test Counts

- FileMonitor tests: 18 tests
- FilesMonitor tests: 25 tests
- Total: 43 tests

### Expected Output

```
[==========] Running 18 tests from 6 test suites.
[----------] 2 tests from TimestampTests
[ RUN      ] TimestampTests.FormatCorrect
[       OK ] TimestampTests.FormatCorrect (0 ms)
[----------] Global test environment tear-down
[==========] 18 tests from 6 test suites ran. (41 ms total)
[  PASSED  ] 18 tests.
```

## CI/CD Testing

### GitHub Actions

```bash
# Run tests in CI
- name: Run tests
  run: |
    ./test/filemonitor_tests
    ./test/filesmonitor_tests
```

### Docker CI Pipeline

```bash
# Build and test in CI
docker build -f docker_stuff/Dockerfile.test -t sparky_siem_test:latest .
docker run --rm sparky_siem_test:latest
```

## Test Coverage

### Unit Tests

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| TimestampTests | 2 | Timestamp formatting, uniqueness |
| MessageFormatTests | 5 | JSON structure, content preservation |
| FileSystemTest | 5 | File operations |
| JsonTests | 2 | JSON parsing |
| StringTests | 3 | String manipulation |
| ThreadTests | 1 | Thread safety |
| PathTest | 3 | Path manipulation |
| FilesMonitorPathTest | 6 | Path management |
| FilesMonitorMapTest | 4 | Map operations |
| FilesMonitorCleanupTest | 3 | Cleanup logic |
| FilesMonitorThreadTest | 3 | Thread simulation |
| FilesMonitorHandleFileTest | 4 | HandleFile logic |
| FilesMonitorIntegrationTest | 2 | Integration |
