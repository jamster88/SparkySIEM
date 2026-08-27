# Build & Run Standalone Tests (No External Dependencies)

## What it tests
FileFormat utilities: formatMessage() JSON escaping, getCurrentTimestamp() UTC formatting

## Test file
- `tests/test_runner.cpp` - Plain C++ test harness (no gtest needed)
- `tests/FileFormat.h` - Header under test

## Build Command
```bash
cd tests
clang++ -std=c++17 -Wall -Wextra -I.. \
  -o test_runner ../FileFormat.cpp test_runner.cpp
```

## Run Command
```bash
./test_runner
```

## Expected Output
```
=== SparkySIEM FileMonitor Unit Tests ===
[formatMessage]
  [RUN]  test_format_message_produces_json... PASS
  ...
[getCurrentTimestamp]
  [RUN]  test_timestamp_format... PASS
  ...
=== Results: 14 total, 14 passed, 0 failed ===
```

## Dependencies
None - pure C++17 standard library only
