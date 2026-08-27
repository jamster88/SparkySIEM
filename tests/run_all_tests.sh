#!/bin/bash
# ==============================================================================
# SparkySIEM FileMonitor - Unified Test Runner
# ==============================================================================
#
# Usage:
#   ./run_all_tests.sh              # Run all tests (standalone + gtest)
#   ./run_all_tests.sh standalone    # Run only standalone tests (no gtest needed)
#   ./run_all_tests.sh gtest         # Run only gtest tests (requires brew install googletest)
#   ./run_all_tests.sh clean         # Clean build artifacts
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/.."
CXX="g++"
CXXFLAGS="-std=c++17 -Wall -Wextra -I$SRC_DIR -I$SCRIPT_DIR -I/opt/homebrew/include -pthread"
GTEST_LIBS="$(pkg-config --cflags --libs gtest 2>/dev/null || echo '-L/opt/homebrew/lib -lgtest -lgtest_main -pthread')"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

pass=0
fail=0

run_test() {
    local name="$1"
    shift
    printf "  ${YELLOW}[RUN]${NC} %-50s" "$name"
    if "$@" > /dev/null 2>&1; then
        printf "${GREEN}[PASS]${NC}\n"
        ((pass++))
    else
        printf "${RED}[FAIL]${NC}\n"
        ((fail++))
        # Print output on failure
        echo "      Output:"
        "$@" 2>&1 | sed 's/^/        /'
    fi
}

do_standalone() {
    echo "=== Standalone Tests (no external deps) ==="
    $CXX $CXXFLAGS -o "$SCRIPT_DIR/test_runner" "$SRC_DIR/FileFormat.cpp" "$SCRIPT_DIR/test_runner.cpp" 2>/dev/null
    run_test "formatMessage: valid JSON, escaping, timestamps" "$SCRIPT_DIR/test_runner"
    rm -f "$SCRIPT_DIR/test_runner"
}

do_gtest_format() {
    echo ""
    echo "=== GoogleTest: FileFormat ==="
    $CXX $CXXFLAGS -o "$SCRIPT_DIR/gtest_file_format" \
        "$SCRIPT_DIR/test_file_format.cpp" "$SRC_DIR/FileFormat.cpp" "$GTEST_LIBS" 2>/dev/null
    local result=0
    run_test "FileFormat (17 tests)" "$SCRIPT_DIR/gtest_file_format" || true
    rm -f "$SCRIPT_DIR/gtest_file_format"
}

do_gtest_filesmon() {
    echo ""
    echo "=== GoogleTest: FilesMonitor Logic ==="
    $CXX $CXXFLAGS -o "$SCRIPT_DIR/gtest_filesmon" \
        "$SCRIPT_DIR/test_files_monitor_logic.cpp" "$SRC_DIR/FileFormat.cpp" "$GTEST_LIBS" 2>/dev/null
    local result=0
    run_test "FilesMonitor Logic (17 tests)" "$SCRIPT_DIR/gtest_filesmon" || true
    rm -f "$SCRIPT_DIR/gtest_filesmon"
}

case "${1:-all}" in
    standalone|all)
        do_standalone
        ;;
    gtest|all)
        do_gtest_format
        do_gtest_filesmon
        ;;
    clean)
        rm -f "$SCRIPT_DIR/test_runner" \
              "$SCRIPT_DIR/gtest_file_format" \
              "$SCRIPT_DIR/gtest_filesmon"
        echo "Cleaned."
        exit 0
        ;;
    *)
        echo "Usage: $0 {all|standalone|gtest|clean}"
        exit 1
        ;;
esac

echo ""
echo "=== Results: $pass passed, $fail failed ==="
[ $fail -eq 0 ] && echo "${GREEN}All tests passed!${NC}" || echo "${RED}Some tests failed!${NC}"
exit $fail
