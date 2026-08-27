# SparkySIEM — Build and Test Commands

Every component in the repository, how to build it, and how to test it. All commands are
run from the repository root unless stated otherwise.

**Platform note:** the monitors use `inotify`, which is Linux only. On macOS or Windows,
build and test through the container workflow (`./run_tests.sh`, or the Docker recipes
below). On Linux, `make` works directly.

---

## 1. SparkySIEM — the forwarder

**Sources:** `main.cpp`, `FileMonitor.{h,cpp}`, `FilesMonitor.{h,cpp}`,
`KafkaSink.{h,cpp}`, `MessageFormat.{h,cpp}`, `MessageSink.h`
**Dependencies:** `librdkafka-dev`, C++17, pthreads

### Build (Linux host)

```sh
make                        # produces build/SparkySIEM
make clean                  # removes build/
make BUILD_DIR=/tmp/b       # build somewhere else
```

Equivalent manual compile:

```sh
g++ -std=c++17 -Wall -Wextra -g -pthread \
    main.cpp MessageFormat.cpp KafkaSink.cpp FileMonitor.cpp FilesMonitor.cpp \
    -o SparkySIEM -lrdkafka++ -lrdkafka
```

### Build (from macOS, in a container)

```sh
docker build -f Dockerfile.test -t sparkysiem-tests .
docker run --rm -v "$PWD":/work:ro -w /work sparkysiem-tests \
    bash -c 'cp -r /work /tmp/s && cd /tmp/s && make BUILD_DIR=/tmp/b && ls -l /tmp/b/SparkySIEM'
```

### Run

```sh
./build/SparkySIEM <broker> <topic> <path> [path...]

# examples
./build/SparkySIEM localhost:9092 my-topic /var/log/app.log
./build/SparkySIEM localhost:9092 my-topic /var/log /etc/myapp/audit.log
```

Each path may be a file or a directory. Directories are rescanned once a second, so files
created later are picked up and deleted files have their monitors shut down. Ctrl-C
(SIGINT) or SIGTERM stops everything cleanly and flushes the producer.

### Verify it built correctly (no warnings)

```sh
docker run --rm -v "$PWD":/work:ro -w /work sparkysiem-tests bash -c \
    'cp -r /work /tmp/s && cd /tmp/s && make BUILD_DIR=/tmp/b 2>&1 | grep -iE "warn|error"; \
     echo "BUILD EXIT: ${PIPESTATUS[0]}"'
```

---

## 2. Unit tests

**Sources:** `tests/test_message_format.cpp`, `tests/test_file_monitor.cpp`,
`tests/test_files_monitor.cpp`, `tests/TestSupport.h`, `tests/FakeSink.h`
**Dependencies:** `libgtest-dev`, `nlohmann-json3-dev`, plus the app's dependencies
**Kafka is NOT required** — the tests publish into an in-memory `FakeSink`.

### Run everything (any host, containerised)

```sh
./run_tests.sh
```

This builds `Dockerfile.test`, mounts the repository **read only**, and builds into a
scratch path inside the container, so it never leaves artifacts in the working tree.

### Run a subset

```sh
./run_tests.sh --gtest_filter='FileMonitorRotation.*'
./run_tests.sh --gtest_filter='*Json*'
./run_tests.sh --gtest_list_tests
```

### Run on a Linux host, directly

```sh
make test
make test GTEST_ARGS="--gtest_filter=FileMonitorIncremental.*"
```

### Check for flaky, timing-sensitive tests

```sh
docker run --rm -v "$PWD":/work:ro -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/sparky-build \
    GTEST_ARGS="--gtest_repeat=3 --gtest_shuffle --gtest_brief=1"
```

Expected: `45 tests from 15 test suites ran. [ PASSED ] 45 tests.` on each repeat.

### Test suites

| Suite prefix | What it covers |
| --- | --- |
| `EscapeJson.*`, `FormatMessage.*`, `CurrentTimestamp.*` | JSON escaping, message fields, timestamp format |
| `FileMonitorConstruction.*` | Missing file, null sink, non-copyable |
| `FileMonitorStartup.*` | INIT messages, existing content |
| `FileMonitorIncremental.*` | Only-new-lines, ordering, partial lines, empty lines, CRLF |
| `FileMonitorMessages.*` | JSON validity for hostile content, path/topic fields |
| `FileMonitorRotation.*` | Truncation, log rotation |
| `FileMonitorFailures.*` | Unreadable file (no flood), publish failures |
| `FileMonitorShutdown.*` | Prompt stop, CLOSE + flush, idempotent stop |
| `FilesMonitor*.*` | Multiple files, directories, late files, deletions, shutdown |

> The `FileMonitorFailures.AnUnreadableFileDoesNotFloodTheSink` test skips itself when run
> as root, because root ignores file permissions. `Dockerfile.test` runs the suite as the
> non-root user `sparky` so it actually executes.

---

## 3. Kafka test environment (`docker_stuff/`)

```sh
cd docker_stuff
docker compose up -d              # start broker (ctest_broker, localhost:9092)
docker logs ctest_broker
docker compose down -v            # stop and delete the volumes
cd ..
```

### Create a topic / produce / consume

```sh
# shell inside the broker
docker exec -it -w /opt/kafka/bin ctest_broker sh

# from the host
docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh \
    --create --topic my-topic --bootstrap-server ctest_broker:29092

docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic my-topic --from-beginning \
    --bootstrap-server ctest_broker:29092 --timeout-ms 8000

# count the messages on a topic
docker exec ctest_broker /opt/kafka/bin/kafka-get-offsets.sh \
    --bootstrap-server ctest_broker:29092 --topic my-topic
```

Addresses: `localhost:9092` from the host, `ctest_broker:29092` from another container on
the `docker_stuff_default` network.

---

## 4. End-to-end check against a real broker

Confirms the forwarder really delivers to Kafka. Not part of the unit suite.

```sh
# 1. broker up
cd docker_stuff && docker compose up -d && cd ..

# 2. build and run the forwarder inside the Kafka network
docker build -f Dockerfile.test -t sparkysiem-tests .
docker run --rm --network docker_stuff_default -v "$PWD":/work:ro sparkysiem-tests bash -c '
    cp -r /work /tmp/src && cd /tmp/src && make BUILD_DIR=/tmp/b -s
    mkdir -p /tmp/logs && printf "line A\n" > /tmp/logs/app.log
    /tmp/b/SparkySIEM ctest_broker:29092 sparky-e2e /tmp/logs &
    APP=$!; sleep 3
    printf "line B\n" >> /tmp/logs/app.log; sleep 1
    printf "he said \"hi\" \\ done\n" >> /tmp/logs/app.log; sleep 1
    mv /tmp/logs/app.log /tmp/logs/app.log.1                  # rotate
    printf "after rotation\n" > /tmp/logs/app.log; sleep 2
    kill -TERM $APP; sleep 2'

# 3. inspect what landed on the topic
docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic sparky-e2e --from-beginning \
    --bootstrap-server ctest_broker:29092 --timeout-ms 8000
```

Expect each line exactly once, `\"hi\" \\` correctly escaped, a `ROTATED` message followed
by `after rotation`, and a `CLOSE` per monitored file.

---

## 5. Synthetic log generator (`rand_data_gen/`)

```sh
cd rand_data_gen
gcc -Wall -o rand_data_gen rand_data_gen.c
./rand_data_gen 10      # writes file.txt with 10 * 100 = 1000 lines
cd ..
```

The argument is multiplied by `CNT_SCALE` (100); with no argument it writes 1000 lines.
Output always goes to `file.txt` in the current directory. Useful for pointing the
forwarder at a file that is actively growing.

---

## 6. Mutation testing (optional, verifies the tests have teeth)

Reintroduce an original defect into a copy of the source and confirm the suite fails.
Uses `misc/`-adjacent scratch copies; never modifies the working tree.

```sh
WORK=$(mktemp -d)
rsync -a --exclude .git --exclude build ./ "$WORK/"

# example: undo the incremental-read fix, so the whole file is resent each time
sed -i '' 's|    openErrorReported = false;\n|&    offset = 0; partialLine.clear();\n|' \
    "$WORK/FileMonitor.cpp"   # or edit FileMonitor.cpp::readNewData by hand

docker run --rm -v "$WORK":/work:ro sparkysiem-tests bash -c \
    'cp -r /work /tmp/mut && cd /tmp/mut && make test BUILD_DIR=/tmp/b \
     GTEST_ARGS="--gtest_filter=FileMonitorIncremental.*"'
```

Expected: the run **fails**. A mutation that passes means the suite is missing a test.

Mutations checked during the verification session, all caught:

| Defect reintroduced | Caught by |
| --- | --- |
| Whole-file resend | `PublishesAnAppendedLineExactlyOnce`, `RepeatedAppendsNeverResendEarlierLines` |
| No JSON escaping | `EscapeJson.*` (4), `FormatMessage.ProducesValidJsonForContentWithQuotesAndBackslashes`, `ContentWithQuotesAndBackslashesStaysValidJson` |
| No rotation handling | `FollowsANewFileThatReplacesTheWatchedPath` |
| Error reported per event | `AnUnreadableFileDoesNotFloodTheSink` |

---

## 7. Quick reference

| Goal | Command |
| --- | --- |
| Build the forwarder | `make` |
| Run the forwarder | `./build/SparkySIEM localhost:9092 my-topic /var/log/app.log` |
| Run all tests (macOS) | `./run_tests.sh` |
| Run all tests (Linux) | `make test` |
| Run one test suite | `./run_tests.sh --gtest_filter='FileMonitorRotation.*'` |
| Start Kafka | `cd docker_stuff && docker compose up -d` |
| Read a topic | `docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh --topic <t> --from-beginning --bootstrap-server ctest_broker:29092 --timeout-ms 8000` |
| Stop Kafka | `cd docker_stuff && docker compose down -v` |
| Generate test data | `cd rand_data_gen && gcc -Wall -o rand_data_gen rand_data_gen.c && ./rand_data_gen 10` |
| Clean build output | `make clean` |
