# Build & Run All Tests (Unified)

## Quick Start
```bash
cd tests
./run_all_tests.sh all        # Run standalone + gtest
./run_all_tests.sh standalone # Standalone only (no deps)
./run_all_tests.sh gtest      # gtest only (requires brew install googletest)
./run_all_tests.sh clean      # Remove build artifacts
```

## What It Does
- Builds and runs standalone test runner (14 tests, no dependencies)
- Builds and runs GoogleTest suites if gtest is installed (34 tests)
- Reports pass/fail counts with color output

## Expected Output
```
=== Standalone Tests ===
  [PASS] formatMessage: valid JSON, escaping, timestamps...
=== Results: 1 passed, 0 failed ===

=== GoogleTest: FileFormat ===
[==========] Running 17 tests from 1 test suite.
[PASSED] 17 tests.

=== GoogleTest: FilesMonitor Logic ===
[==========] Running 17 tests from 1 test suite.
[PASSED] 17 tests.

=== Results: 34 passed, 0 failed ===
All tests passed!
```

## Dependencies
- C++17 compiler (g++ or clang++)
- GoogleTest: `brew install googletest` (for gtest tests only)
