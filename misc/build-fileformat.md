# Build FileFormat (Pure Utility Layer)

## What it is
Platform-independent JSON message formatting and timestamp generation utilities. No external dependencies required.

## Files
- `FileFormat.h` - Header with function declarations
- `FileFormat.cpp` - Implementation with full JSON escaping

## Build Command
```bash
clang++ -std=c++17 -Wall -Wextra -c FileFormat.cpp -o FileFormat.o
```

## Linking (example)
```bash
clang++ -std=c++17 -Wall main.cpp FileFormat.o -o myapp
```
