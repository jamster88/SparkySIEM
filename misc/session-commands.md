# All Terminal Commands from This Session

## Discovery & Setup Commands
```bash
# List project files
find /Users/jamster/Repos/SparkySIEM -type f | head -80

# Check compiler and librdkafka
which gcc g++ 2>/dev/null; gcc --version 2>/dev/null | head -1; pkg-config --libs librdkafka++ 2>/dev/null || echo "librdkafka++ not found via pkg-config"; ls /usr/local/lib/librdkafka* 2>/dev/null || echo "no librdkafka in /usr/local/lib"; ls /opt/homebrew/lib/librdkafka* 2>/dev/null || echo "no librdkafka in homebrew"; brew list | grep rdkafka 2>/dev/null || echo "rdkafka not in brew"

# Check Docker availability
docker ps 2>/dev/null; docker info 2>/dev/null | head -5 || echo "Docker not available or not running"

# Check for existing test infrastructure
ls /Users/jamster/Repos/SparkySIEM/tests/ 2>/dev/null || echo "No tests directory yet"; ls /Users/jamster/Repos/SparkySIEM/test/*.cpp 2>/dev/null || echo "No existing test files"

# Check for librdkafka C++ header
ls /opt/homebrew/include/librdkafka/rdkafkacpp.h 2>/dev/null || echo "rdkafkacpp.h not in homebrew include"; brew --prefix librdkafka 2>/dev/null; find /opt/homebrew -name "rdkafkacpp.h" 2>/dev/null

# Compile existing code to find errors
gcc -std=c++17 -Wall -I/opt/homebrew/include -L/opt/homebrew/lib FilesMonitor.cpp main.cpp -o SparkySIEM_test_build -lrdkafka -lpthread -lz -levent -lcrypto -lzstd -lsasl2 -ldl 2>&1

# Verify inotify not available on macOS
ls /usr/include/sys/inotify.h 2>/dev/null || echo "inotify.h not found on macOS - confirmed Linux-only API"

# Install GoogleTest framework
brew install googletest 2>&1 | tail -5

# Check gtest installation
brew list --versions googletest 2>/dev/null; echo "---"; ls /opt/homebrew/include/gtest/*.h 2>/dev/null | head -5 || echo "not in homebrew include"; ls /opt/homebrew/lib/libg* 2>/dev/null || echo "no gtest libs"

# Create tests directory
mkdir -p /Users/jamster/Repos/SparkySIEM/tests

# Install GoogleTest (second attempt after timeout)
brew install googletest 2>&1 | tail -10

# Verify gtest install
ls /opt/homebrew/include/gtest/gtest.h 2>/dev/null || echo "not found"; ls /opt/homebrew/lib/libgtest.a 2>/dev/null || echo "lib not found"; brew list --versions googletest 2>/dev/null || echo "not in brew"

# Install GoogleTest (third attempt)
brew install googletest 2>&1 | tail -10
```

## Build Commands
```bash
# Compile standalone test runner (no deps)
clang++ -std=c++17 -Wall -Wextra -I.. -o test_runner ../FileFormat.cpp test_runner.cpp 2>&1

# Run standalone tests
./test_runner 2>&1

# Compile gtest file format tests
g++ -std=c++17 -Wall -Wextra -I../ -I./ -I/opt/homebrew/include test_file_format.cpp ../FileFormat.cpp $(pkg-config --cflags --libs gtest 2>/dev/null || echo "-L/opt/homebrew/lib -lgtest -lgtest_main -pthread") -o gtest_file_format 2>&1

# Run gtest file format tests
./gtest_file_format --gtest_color=yes 2>&1

# Compile FilesMonitor logic tests (after refactoring to be platform-independent)
g++ -std=c++17 -Wall -Wextra -I../ -I./ -I/opt/homebrew/include test_files_monitor_logic.cpp ../FileFormat.cpp $(pkg-config --cflags --libs gtest 2>/dev/null || echo "-L/opt/homebrew/lib -lgtest -lgtest_main -pthread") -o gtest_filesmon 2>&1

# Run FilesMonitor logic tests
./gtest_filesmon --gtest_color=yes 2>&1

# Final verification - run all tests together
echo "=== Standalone Tests ===" && ./test_runner 2>&1 && echo "" && echo "=== GoogleTest: FileFormat ===" && ./gtest_file_format --gtest_color=yes 2>&1 && echo "" && echo "=== GoogleTest: FilesMonitor Logic ===" && ./gtest_filesmon --gtest_color=yes 2>&1

# Clean up temporary binaries
rm -f /Users/jamster/Repos/SparkySIEM/tests/gtest_file_format /Users/jamster/Repos/SparkySIEM/tests/gtest_filesmon /Users/jamster/Repos/SparkySIEM/tests/test_runner && echo "Cleaned up"

# Create misc directory
mkdir -p /Users/jamster/Repos/SparkySIEM/misc
```

## File Inspection Commands (for reading project files)
```bash
# Read all source files in one check
Read: FilesMonitor.h, FilesMonitor.cpp, FileMonitor.h, FileMonitor.cpp, main.cpp, ConfigReader.cpp, README.md
Read: docker_stuff/compose.yaml, rand_data_gen/rand_data_gen.c, rand_data_gen/file.txt, .vscode/tasks.json
Read: docker_stuff/docker_commands_for_cli, docker_stuff/commands

# Check exact bytes of test file for debugging escape issues
sed -n '132,138p' /Users/jamster/Repos/SparkySIEM/tests/test_file_format.cpp | xxd

# Fix failing test assertions with Python
cat > /tmp/fix_tests.py << 'PYEOF'
import re
# ... (Python script to fix test escape sequence assertions)
PYEOF
python3 /tmp/fix_tests.py 2>&1

# Verify exact bytes in test files for debugging
sed -n '130,142p' /Users/jamster/Repos/SparkySIEM/tests/test_file_format.cpp | xxd
```

## Misc Diagnostic Commands
```bash
# Check gtest install status (repeated timeout checks)
brew list --versions googletest 2>/dev/null; echo "---"; ls /usr/local/include/gtest/*.h 2>/dev/null || echo "not in /usr/local"
ps aux | grep -i brew | grep -v grep
```
