# ==============================================================================
# Makefile for SparkySIEM FileMonitor unit tests
# ==============================================================================
#
# Build: make all
# Run:   make test
# Clean: make clean
#
# Requirements:
#   - C++17 compiler (g++ or clang++)
#   - GoogleTest framework (brew install googletest)
#   - librdkafka (for production code, tests use mocks only)
#
# ==============================================================================

CXX      = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -I../ -I./ -I/opt/homebrew/include -pthread
LDFLAGS  = -L/opt/homebrew/lib -lgtest -lgtest_main -lpthread

# Source files (tests don't depend on FileMonitor.cpp which needs inotify)
TEST_SRCS = test_file_format.cpp test_files_monitor_logic.cpp ../FileFormat.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

# Test executable
TEST_BIN  = sparky_siemon_tests

# Directories
SRC_DIR   = ..
TEST_DIR  = .

.PHONY: all test clean

all: $(TEST_BIN)

$(TEST_BIN): $(TEST_OBJS)
	@echo "Linking $(TEST_BIN)..."
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_OBJS) $(LDFLAGS)
	@echo "Build complete. Run 'make test' to execute tests."

%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Also compile .cpp files in parent directory that are needed (no inotify deps for these)
../FileFormat.o: ../FileFormat.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(TEST_BIN)
	@echo "Running tests..."
	./$(TEST_BIN) --gtest_color=yes

clean:
	@echo "Cleaning build artifacts..."
	rm -f $(TEST_OBJS) $(TEST_DIR)/../FileFormat.o $(TEST_BIN)
	@echo "Done."

# ==============================================================================
# Individual test targets (run specific test suite only)
# ==============================================================================

test_format: test_file_format.o ../FileFormat.o
	$(CXX) $(CXXFLAGS) -o format_test test_file_format.o ../FileFormat.o $(LDFLAGS)
	./format_test --gtest_color=yes

test_filesmon: test_files_monitor_logic.o ../FileFormat.o
	$(CXX) $(CXXFLAGS) -o filesmon_test test_files_monitor_logic.o ../FileFormat.o $(LDFLAGS)
	./filesmon_test --gtest_color=yes

clean_tests: clean
	rm -f format_test filesmon_test
