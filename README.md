# SparkySIEM

The goal is replicate the **very** basic functions of a Splunk Forwarder:
* point at file (or directory)
* monitor for changes
* send changes to some location - *Kafka in this case*

Eventually the goal is to add more and more features that a present on the Splunk forwarder like:
* employ TLS encryption with certificates to protect comms and verify producer/consumer
* config files
* remote config - *eg call 'home' to a central config server to get their marching orders*
* remote deployment - *deploy with TLS encryption and certs to enable secure remote config*

Finally, the goal will be to enable features that *__aren't__* present in the Splunk forwarder like custom message formats and anything anyone else can think of.


## Consumption

This is designed to send the data to a Kafka instance with the expectation that it will be consumed by a Spark Streaming job, however it could be consumed by other tools like Beam, Flink, etc.


## TO-DO

* Feature work planning
* *flesh this out*

---

## Testing

Unit tests are in the `tests/` directory. Two test suites are provided:

### 1. Standalone tests (no external dependencies)
```bash
cd tests
g++ -std=c++17 -I.. -o test_runner FileFormat.cpp test_runner.cpp && ./test_runner
```

### 2. GoogleTest suite (requires gtest)
```bash
brew install googletest
cd tests
./run_all_tests.sh      # Run all tests
./run_all_tests.sh gtest   # gtest only
./run_all_tests.sh standalone  # standalone only
```

### Test coverage
| Suite | Tests | What's tested |
|-------|-------|----------------|
| FileFormat (gtest) | 17 | Message JSON formatting, timestamp generation, escape handling |
| FilesMonitor Logic (gtest) | 17 | Directory scanning, file detection, cleanup algorithm, thread safety |
| Standalone runner | 14 | formatMessage, getCurrentTimestamp, edge cases |

### Bugs fixed during test development
1. **FilesMonitor passed wrong args to FileMonitor** - missing kafkaBroker parameter (2 args instead of 3)
2. **formatMessage() produced invalid JSON** - no escaping of special characters (now in FileFormat.cpp)
3. **getCurrentTimestamp() used local time** - changed to UTC for distributed-system compatibility
4. **monitor() dead code** after infinite while-loop (CLOSE/flush never executed)

---

## docker_stuff

