# Build FilesMonitor (Multi-File/Directory Monitor with Kafka)

## What it is
Monitors multiple files and directories for changes, creates a FileMonitor per file, sends all updates to Kafka.

## Files
- `FilesMonitor.h` - Header declaration
- `FilesMonitor.cpp` - Implementation

## Dependencies
- librdkafka++ (`brew install librdkafka`)
- inotify (Linux only)
- C++17 filesystem support

## Build Command
```bash
clang++ -std=c++17 -Wall -Wextra \
  -I/opt/homebrew/include \
  FilesMonitor.cpp FileFormat.cpp \
  -o files_monitor \
  -L/opt/homebrew/lib -lrdkafka \
  -lpthread -lz -lcrypto -lzstd -lsasl2 -ldl
```

## Run (requires Kafka broker)
```bash
# Default: localhost:9092, monitors hardcoded paths
./files_monitor
```
