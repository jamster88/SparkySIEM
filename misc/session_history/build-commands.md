# Build Commands for SparkySIEM

## Prerequisites

- C++17 compatible compiler (g++ 7+ or clang++ 5+)
- librdkafka development library
- Google Test (gtest) development library

## Building on Linux

### Step 1: Install Dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y g++ make cmake git wget curl pkg-config
sudo apt-get install -y librdkafka-dev

# Install Google Test
sudo apt-get install -y libgtest-dev
cd /usr/src/googletest
sudo mkdir build && cd build
sudo cmake ..
sudo make
sudo make install
```

### Step 2: Build the File Monitor

```bash
# Navigate to project directory
cd /Users/jamster/Repos/SparkySIEM

# Compile FileMonitor.cpp
g++ -std=c++17 -I/usr/include -c FileMonitor.cpp -o FileMonitor.o

# Compile main.cpp and link
g++ -std=c++17 -I/usr/include main.cpp FileMonitor.o -lrdkafka -o sparky_siem
```

### Step 3: Build Tests

```bash
# Compile FileMonitor tests
g++ -std=c++17 -I/usr/include test/standalone_tests.cpp /usr/lib/x86_64-linux-gnu/libgtest.a -o test/filemonitor_tests -pthread

# Compile FilesMonitor tests
g++ -std=c++17 -I/usr/include test/standalone_filesmonitor_tests.cpp /usr/lib/x86_64-linux-gnu/libgtest.a -o test/filesmonitor_tests -pthread
```

## Building with Docker

### Build Image

```bash
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker build -t sparky_siem:latest -f Dockerfile ..
```

### Build Test Image

```bash
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker build -t sparky_siem_test:latest -f Dockerfile.test ..
```

## Building on macOS (for code review only)

Note: The code uses Linux-specific inotify API and will not compile natively on macOS.

### Workaround: Use Docker on macOS

```bash
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker build -t sparky_siem:latest -f Dockerfile ..
```

### Workaround: Use WSL2 on Windows

Run the Linux build instructions inside WSL2.

## Makefile Targets

```bash
cd /Users/jamster/Repos/SparkySIEM/test

# Build all tests
make all

# Build FileMonitor tests
make filemonitor

# Build FilesMonitor tests
make filesmonitor

# Run all tests
make run

# Clean
make clean
```

## Clean Build

```bash
# Remove build artifacts
rm -f FileMonitor.o sparky_siem
rm -f test/*.o test/*_tests

# Rebuild
make
```

## Verifying the Build

```bash
# Check binary exists and is executable
ls -la sparky_siem

# Check binary dependencies
ldd sparky_siem

# Run tests
./test/filemonitor_tests
./test/filesmonitor_tests
```
