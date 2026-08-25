# Build & Run GoogleTest Suite (Requires brew install googletest)

## Prerequisites
```bash
brew install googletest
```

## Test Suites

### 1. FileFormat Tests (17 tests)
Tests: JSON validity, escape handling (backslash, quote, newline, tab, control chars), timestamp format/UTC/uniqueness, empty strings, special topic names

**Build:**
```bash
cd tests
g++ -std=c++17 -Wall -Wextra \
  -I../ -I./ -I/opt/homebrew/include \
  test_file_format.cpp ../FileFormat.cpp \
  $(pkg-config --cflags --libs gtest 2>/dev/null || echo "-L/opt/homebrew/lib -lgtest -lgtest_main -pthread") \
  -o gtest_file_format
```

**Run:**
```bash
./gtest_file_format --gtest_color=yes
```

### 2. FilesMonitor Logic Tests (17 tests)
Tests: directory scanning, file detection, handleFile deduplication, cleanupDeletedFiles algorithm, concurrent thread safety, monitor start/stop

**Build:**
```bash
cd tests
g++ -std=c++17 -Wall -Wextra \
  -I../ -I./ -I/opt/homebrew/include \
  test_files_monitor_logic.cpp ../FileFormat.cpp \
  $(pkg-config --cflags --libs gtest 2>/dev/null || echo "-L/opt/homebrew/lib -lgtest -lgtest_main -pthread") \
  -o gtest_filesmon
```

**Run:**
```bash
./gtest_filesmon --gtest_color=yes
```

## Combined Test Run (All 34 gtest tests)
```bash
cd tests
./run_all_tests.sh gtest
```

## Expected Results
- FileFormat: 17 passed, 0 failed
- FilesMonitor Logic: 17 passed, 0 failed
- Total: 34 passed, 0 failed
