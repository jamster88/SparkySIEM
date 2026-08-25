# Makefile for SparkySIEM Tests
# Supports both macOS and Linux builds

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

# GTest paths
GTEST_INCLUDE = /tmp/googletest-1.14.0/googletest/include
GTEST_LIB = /tmp/googletest-1.14.0/build/lib
GTEST_MAIN = $(GTEST_LIB)/libgtest_main.a

# Source files
TEST_DIR = .
SRC_FILES = $(TEST_DIR)/standalone_tests.cpp $(TEST_DIR)/standalone_filesmonitor_tests.cpp

# Output
FILEMONITOR_TESTS = filemonitor_tests
FILESMONITOR_TESTS = filesmonitor_tests
ALL_TESTS = all_tests

# Default target
all: $(ALL_TESTS)

# FileMonitor tests (standalone tests)
$(FILEMONITOR_TESTS): $(TEST_DIR)/standalone_tests.cpp $(GTEST_MAIN)
	$(CXX) $(CXXFLAGS) \
		-I$(GTEST_INCLUDE) \
		-I$(TEST_DIR) \
		$(TEST_DIR)/standalone_tests.cpp \
		$(GTEST_MAIN) \
		-lpthread \
		-o $@

# FilesMonitor tests (standalone tests)
$(FILESMONITOR_TESTS): $(TEST_DIR)/standalone_filesmonitor_tests.cpp $(GTEST_MAIN)
	$(CXX) $(CXXFLAGS) \
		-I$(GTEST_INCLUDE) \
		-I$(TEST_DIR) \
		$(TEST_DIR)/standalone_filesmonitor_tests.cpp \
		$(GTEST_MAIN) \
		-lpthread \
		-o $@

# All tests combined
$(ALL_TESTS): $(SRC_FILES) $(GTEST_MAIN)
	$(CXX) $(CXXFLAGS) \
		-I$(GTEST_INCLUDE) \
		-I$(TEST_DIR) \
		$(SRC_FILES) \
		$(GTEST_MAIN) \
		-lpthread \
		-o $@

# Run tests
run: $(ALL_TESTS)
	./$(ALL_TESTS)

run-filemonitor: $(FILEMONITOR_TESTS)
	./$(FILEMONITOR_TESTS)

run-filesmonitor: $(FILESMONITOR_TESTS)
	./$(FILESMONITOR_TESTS)

# Clean
clean:
	rm -f $(FILEMONITOR_TESTS) $(FILESMONITOR_TESTS) $(ALL_TESTS)

# Help
help:
	@echo "SparkySIEM Test Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all tests"
	@echo "  filemonitor      - Build FileMonitor tests"
	@echo "  filesmonitor     - Build FilesMonitor tests"
	@echo "  all-tests        - Build all tests combined"
	@echo "  run              - Run all tests"
	@echo "  run-filemonitor  - Run FileMonitor tests"
	@echo "  run-filesmonitor - Run FilesMonitor tests"
	@echo "  clean            - Remove built binaries"
	@echo "  help             - Show this help"
	@echo ""
	@echo "Requirements:"
	@echo "  - GTest installed at /tmp/googletest-1.14.0"
	@echo "  - C++17 compatible compiler"
	@echo ""
	@echo "To install GTest (if not already installed):"
	@echo "  cd /tmp && curl -L https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz -o gtest.tar.gz"
	@echo "  tar -xzf gtest.tar.gz && cd googletest-1.14.0 && mkdir build && cd build && cmake .. && make"

.PHONY: all run run-filemonitor run-filesmonitor clean help
