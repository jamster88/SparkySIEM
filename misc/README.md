# SparkySIEM - Misc Build & Test Documentation

## Directory Index

### Session Record
- [session-summary.md](session-summary.md) - Bug fixes, files created/modified, test results
- [session-commands.md](session-commands.md) - All terminal commands used in this session

### Production Builds
- [build-fileformat.md](build-fileformat.md) - FileFormat utility library
- [build-filemonitor.md](build-filemonitor.md) - Single file monitor (inotify + Kafka)
- [build-filesmonitor.md](build-filesmonitor.md) - Multi-file/directory monitor

### Test Builds
- [build-test-standalone.md](build-test-standalone.md) - Standalone tests (14 tests, no deps)
- [build-test-gtest.md](build-test-gtest.md) - GoogleTest suite (34 tests, requires brew install googletest)
- [build-test-all.md](build-test-all.md) - Unified runner script

### Integration Testing
- [docker-kafka-setup.md](docker-kafka-setup.md) - Docker Kafka broker for end-to-end testing

## Quick Reference: Build Everything
```bash
# Production builds (Linux only - requires inotify + librdkafka)
clang++ -std=c++17 FileMonitor.cpp FileFormat.cpp -o file_monitor -L/opt/homebrew/lib -lrdkafka -lpthread -lz
clang++ -std=c++17 FilesMonitor.cpp FileFormat.cpp -o files_monitor -L/opt/homebrew/lib -lrdkafka -lpthread -lz

# Test builds (macOS compatible)
cd tests && clang++ -std=c++17 -I.. test_runner.cpp ../FileFormat.cpp -o test_runner && ./test_runner
```

## Quick Reference: Test Everything
```bash
cd tests && ./run_all_tests.sh all
```
