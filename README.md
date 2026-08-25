# SparkySIEM

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://github.com/Jamster88/SparkySIEM)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A lightweight file monitoring system that detects changes and sends events to Apache Kafka.

## Overview

SparkySIEM is designed to replicate the **very** basic functions of a Splunk Forwarder:

- Point at file(s) or directory(ies)
- Monitor for changes (create, modify, delete)
- Send changes to a Kafka topic

The project is written in C++17 using:
- **inotify** for file system event monitoring (Linux)
- **librdkafka** for Kafka integration
- **Google Test (gtest)** for unit testing

## Features

- **Single file monitoring**: Monitor a single file for modifications
- **Multiple file monitoring**: Monitor multiple files and directories
- **Kafka integration**: Send file change events to Kafka topics
- **JSON message format**: Structured events with timestamps and metadata
- **Thread-safe**: Uses mutexes for safe concurrent access
- **Automatic cleanup**: Removes monitors for deleted files

## TO-DO

- Finish the barebones version (monitoring, Kafka integration)
- Write comprehensive tests
- Feature work planning
- Config file support
- TLS encryption with certificates
- Remote config server

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         SparkySIEM                             │
│                                                                 │
│  ┌──────────────────┐         ┌──────────────────┐             │
│  │   FileMonitor    │         │   FilesMonitor   │             │
│  │  (single file)   │         │ (multiple files) │             │
│  └────────┬─────────┘         └────────┬─────────┘             │
│           │                            │                        │
│           ▼                            ▼                        │
│  ┌──────────────────┐         ┌──────────────────┐             │
│  │   inotify        │         │   inotify        │             │
│  │   (Linux API)    │         │   (Linux API)    │             │
│  └────────┬─────────┘         └────────┬─────────┘             │
│           │                            │                        │
│           ▼                            ▼                        │
│  ┌──────────────────┐         ┌──────────────────┐             │
│  │    librdkafka    │         │    librdkafka    │             │
│  │   (Kafka client) │         │   (Kafka client) │             │
│  └────────┬─────────┘         └────────┬─────────┘             │
│           │                            │                        │
│           ▼                            ▼                        │
│  ┌──────────────────────────────────────────────┐              │
│  │              Kafka Topic                     │              │
│  │  (e.g., my-topic)                            │              │
│  └──────────────────────────────────────────────┘              │
│                                                                 │
│  ┌──────────────────────────────────────────────┐              │
│  │           Kafka Consumer                     │              │
│  │  (Spark Streaming, Flink, etc.)              │              │
│  └──────────────────────────────────────────────┘              │
└─────────────────────────────────────────────────────────────────┘
```

## Building

### Prerequisites

- C++17 compatible compiler (g++ 9+, clang++ 9+)
- librdkafka library
- Google Test (gtest) for testing

### Building on Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y librdkafka-dev libgtest-dev build-essential cmake

# Build the project
g++ -std=c++17 -o sparkysiem FileMonitor.cpp main.cpp \
    -lrdkafka -lpthread \
    -I.

# Build with FilesMonitor
g++ -std=c++17 -o sparkysiem FilesMonitor.cpp FileMonitor.cpp main.cpp \
    -lrdkafka -lpthread \
    -I. -std=c++17
```

### Building with Docker

```bash
# Build the Docker image
cd docker_stuff
docker build -f Dockerfile -t sparkysiem:latest ..

# Run the container
docker run --network=host sparkysiem:latest /app/sparkysiem /path/to/file localhost:9092 my-topic
```

## Running

### Basic Usage

```bash
# Monitor a single file with default Kafka settings
./sparkysiem /path/to/file.txt

# Monitor with custom Kafka broker and topic
./sparkysiem /path/to/file.txt localhost:9092 my-topic
```

### Using FilesMonitor (Multiple Files)

```bash
# The FilesMonitor class can be used to monitor multiple files/directories
# See test/standalone_filesmonitor_tests.cpp for usage examples
```

## Testing

### Running Tests

```bash
# Build and run tests
g++ -std=c++17 -o test_standalone test/standalone_tests.cpp \
    -lgtest -lgtest_main -lpthread

./test_standalone

# Run FilesMonitor tests
g++ -std=c++17 -o test_files test/standalone_filesmonitor_tests.cpp \
    -lgtest -lgtest_main -lpthread

./test_files
```

### Test Commands

```bash
# All tests
./test_standalone --gtest_filter=*.*

# Specific test
./test_standalone --gtest_filter=TimestampTests.FormatCorrect

# Exclude slow tests
./test_standalone --gtest_filter=-*Slow*
```

### Test Structure

The test directory (`test/`) contains:

| File | Description |
|------|-------------|
| `standalone_tests.cpp` | Core tests for timestamp, message formatting, file operations |
| `standalone_filesmonitor_tests.cpp` | FilesMonitor-specific tests for path/map management |
| `FileMonitor_test.cpp` | FileMonitor class tests with gtest fixtures |
| `FilesMonitor_test.cpp` | FilesMonitor class tests with directory/file operations |
| `MockFileMonitor_test.cpp` | Mock-based tests for isolation |

All standalone tests run without platform-specific dependencies (inotify/Kafka) and are cross-platform.

## Docker Setup

### Kafka Environment

The `docker_stuff` directory contains:

- `Dockerfile` - Main SparkySIEM build
- `Dockerfile.test` - Test runner container
- `docker-compose.yml` - Kafka + SparkySIEM services

### Starting Kafka with Docker Compose

```bash
cd docker_stuff
docker-compose up -d

# Wait for Kafka to be ready
sleep 10

# Run the monitor
docker-compose run sparkysiem /app/sparkysiem /data/test.txt kafka:9092 my-topic
```

### Cleanup

```bash
# Stop all containers
docker-compose down

# Remove volumes (Kafka data)
docker-compose down -v
```

## Message Format

Events are sent to Kafka as JSON strings:

```json
{
  "timestamp": "2025-04-04 14:30:25.123",
  "filePath": "/path/to/file.txt",
  "kafkaTopic": "my-topic",
  "message": "modified content line",
  "type": "MODIFY"
}
```

### Message Types

- `INIT` - Monitor started
- `MODIFY` - File was modified
- `INIT - FILE OPEN` - File verified accessible
- `ERROR - FILE OPEN` - Failed to open file
- `CLOSE` - Monitor stopped

## Configuration

Currently, configuration is passed via command-line arguments:

```
Usage: ./sparkysiem [file_path] [kafka_broker] [kafka_topic]

Arguments:
  file_path     Path to the file to monitor (default: /Users/jamster/Repos/SparkySIEM/test.txt)
  kafka_broker  Kafka broker address (default: localhost:9092)
  kafka_topic   Kafka topic name (default: my-topic)
```

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Author

Jamster88 (mcfadden@auburn.edu)

## Acknowledgments

- [librdkafka](https://github.com/edenhill/librdkafka) - The Kafka C/C++ client library
- [Google Test](https://github.com/google/googletest) - The C++ testing framework
