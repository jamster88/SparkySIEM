# Build FileMonitor (Single-File Monitor with Kafka)

## What it is
Monitors a single file for changes using inotify (Linux only) and sends updates to Kafka.

## Files
- `FileMonitor.h` - Header declaration
- `FileMonitor.cpp` - Implementation

## Dependencies
- librdkafka++ (`brew install librdkafka`)
- inotify (Linux only)
- pthread, dl

## Build Command
```bash
clang++ -std=c++17 -Wall -Wextra \
  -I/opt/homebrew/include \
  FileMonitor.cpp \
  FileFormat.cpp \
  -o file_monitor \
  -L/opt/homebrew/lib -lrdkafka \
  -lpthread -lz -lcrypto -lzstd -lsasl2 -ldl
```

## Run (requires Kafka broker)
```bash
./file_monitor   # hardcoded path in main.cpp: /home/jamster/Repos/SparkySIEM/test.txt
```
