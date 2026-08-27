# SparkySIEM build.
#
#   make            build the SparkySIEM binary
#   make test       build and run the unit tests
#   make clean      remove build output
#
# inotify is Linux only, so this builds on Linux. On macOS or Windows use
# ./run_tests.sh, which runs the same targets inside a Linux container.

CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -Wall -Wextra -g -pthread
BUILD_DIR ?= build

KAFKA_LIBS := -lrdkafka++ -lrdkafka
GTEST_LIBS := -lgtest -lgtest_main

LIB_SRCS  := MessageFormat.cpp KafkaSink.cpp FileMonitor.cpp FilesMonitor.cpp
MAIN_SRC  := main.cpp
TEST_SRCS := tests/test_message_format.cpp \
             tests/test_file_monitor.cpp \
             tests/test_files_monitor.cpp

LIB_OBJS  := $(LIB_SRCS:%.cpp=$(BUILD_DIR)/%.o)
MAIN_OBJ  := $(MAIN_SRC:%.cpp=$(BUILD_DIR)/%.o)
TEST_OBJS := $(TEST_SRCS:%.cpp=$(BUILD_DIR)/%.o)

APP       := $(BUILD_DIR)/SparkySIEM
TEST_BIN  := $(BUILD_DIR)/run_tests

.PHONY: all test clean

all: $(APP)

$(APP): $(LIB_OBJS) $(MAIN_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(KAFKA_LIBS)

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(KAFKA_LIBS) $(GTEST_LIBS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

# GTEST_ARGS is passed straight to the test binary, e.g.
#   make test GTEST_ARGS=--gtest_filter='FileMonitorRotation.*'
GTEST_ARGS ?=

test: $(TEST_BIN)
	$(TEST_BIN) $(GTEST_ARGS)

clean:
	rm -rf $(BUILD_DIR)
