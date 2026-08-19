# SparkySIEM - build and test commands

Every command needed to build, test, and manually exercise the project. All of
these were run and verified on 2026-08-18 (host: macOS 27 / arm64, Docker
29.7.2; container: debian:bookworm-slim, g++ 12, CMake 3.25.1, GoogleTest 1.12.1,
librdkafka 2.0.2).

Run everything from the repository root unless stated otherwise.

---

## 1. Prerequisites

The monitor uses **inotify**, which is Linux-only, so on macOS or Windows the
build and the tests must run in a container. Docker is the only host-side
requirement.

```sh
docker version --format '{{.Server.Version}}'   # confirm the daemon is up
open -a Docker                                  # macOS: start it if it is not
```

---

## 2. Build and test - the normal path

One command builds the image, configures, compiles, and runs all 44 tests:

```sh
./docker_stuff/run_tests.sh
```

Expected tail: `100% tests passed, 0 tests failed out of 44` (about 19 s once
the image is cached; the first run also builds the image, which takes several
minutes on arm64).

Any arguments are forwarded to the GoogleTest binary:

```sh
./docker_stuff/run_tests.sh --gtest_filter='FileMonitorTailing.*'
./docker_stuff/run_tests.sh --gtest_list_tests
./docker_stuff/run_tests.sh --gtest_repeat=5 --gtest_filter='FileMonitorRotation.*'
```

### Build the test image on its own

```sh
docker build -t sparky-siem-test:latest -f docker_stuff/Dockerfile.test docker_stuff
```

---

## 3. Build and test step by step

Useful when a compile error or a hang needs to be seen as it happens, since
`run_tests.sh` wraps all three stages.

```sh
# Configure
docker run --rm -v "$PWD:/work" -w /work sparky-siem-test:latest \
    cmake -S . -B build/docker -DCMAKE_BUILD_TYPE=Debug

# Compile
docker run --rm -v "$PWD:/work" -w /work sparky-siem-test:latest \
    bash -c 'cmake --build build/docker --parallel $(nproc)'

# Test, via ctest (one process per test case)
docker run --rm -v "$PWD:/work" -w /work/build/docker sparky-siem-test:latest \
    ctest --output-on-failure

# Test, via the binary directly (streams progress; better for diagnosing a hang)
docker run --rm -v "$PWD:/work" -w /work sparky-siem-test:latest \
    ./build/docker/tests/sparky_tests
```

`ctest` buffers each test's output until that test finishes, so a hung test
looks like total silence. The binary prints `[ RUN ]` immediately, which is how
the blocking-self-pipe deadlock was located. Piping either through `tail` or
`head` reintroduces the buffering - redirect to a file and watch it instead:

```sh
docker run --rm -v "$PWD:/work" -w /work sparky-siem-test:latest \
    ./build/docker/tests/sparky_tests > /tmp/tests.log 2>&1 &
tail -f /tmp/tests.log
```

### Test groups

```sh
--gtest_filter='JsonEscape.*:FormatMessage.*:CurrentTimestamp.*'  # pure formatting, instant
--gtest_filter='TestSupportJson.*'        # self-tests for the test helpers
--gtest_filter='FileMonitor*'             # 20 monitor tests, ~15 s
--gtest_filter='KafkaSink*'               # 7 sink tests, ~6 s, in-process mock broker
```

---

## 4. Native Linux build

Needs `librdkafka-dev` and `libgtest-dev`:

```sh
sudo apt-get install -y g++ cmake libgtest-dev librdkafka-dev

cmake -S . -B build && cmake --build build --parallel
cd build && ctest --output-on-failure
```

Build without the test suite:

```sh
cmake -S . -B build -DSPARKY_BUILD_TESTS=OFF && cmake --build build
```

Straight g++, no CMake:

```sh
g++ -g -std=c++17 main.cpp FileMonitor.cpp KafkaSink.cpp Message.cpp \
    -o SparkySIEM -lrdkafka -lrdkafka++
```

Configuring on macOS deliberately fails with a `FATAL_ERROR` pointing at
`run_tests.sh`, rather than failing later on a missing `<sys/inotify.h>`.

---

## 5. Running the monitor

```sh
./sparky_siem <file> <broker> <topic>
./sparky_siem /var/log/app.log localhost:9092 my-topic
./sparky_siem                    # defaults: ./test.txt localhost:9092 my-topic
```

`Ctrl-C` (or `SIGTERM`) shuts down cleanly: it emits a `CLOSE` message and
flushes whatever is still queued.

---

## 6. Manual end-to-end test against a real broker

### Start the broker

```sh
cd docker_stuff && docker compose up -d && cd ..
docker ps --filter name=ctest_broker --format '{{.Names}} {{.Status}}'
```

### Topic and consumer

```sh
docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh \
    --bootstrap-server localhost:9092 --create --topic my-topic \
    --partitions 1 --replication-factor 1

docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh \
    --bootstrap-server localhost:9092 --list

# Current end offset - the authoritative count of what was produced
docker exec ctest_broker /opt/kafka/bin/kafka-get-offsets.sh \
    --bootstrap-server localhost:9092 --topic my-topic

# Consume everything
docker exec -d ctest_broker bash -c '/opt/kafka/bin/kafka-console-consumer.sh \
    --bootstrap-server localhost:9092 --topic my-topic --from-beginning \
    > /tmp/consumed.txt 2>/dev/null'

# Or consume only from a known offset, to ignore earlier runs
docker exec -d ctest_broker bash -c '/opt/kafka/bin/kafka-console-consumer.sh \
    --bootstrap-server localhost:9092 --topic my-topic --partition 0 --offset 21 \
    > /tmp/consumed.txt 2>/dev/null'

docker exec ctest_broker cat /tmp/consumed.txt
```

Kill stray consumers before starting a new one - several writing to the same
path produce output that looks like duplicated messages when it is not:

```sh
docker exec ctest_broker pkill -f ConsoleConsumer
```

### Run the monitor against that broker

`--network container:ctest_broker` shares the broker's network namespace so
`localhost:9092` resolves.

```sh
docker run --rm --network container:ctest_broker \
    -v "$PWD:/work" -w /work sparky-siem-test:latest bash -c '
        mkdir -p /tmp/e2e && : > /tmp/e2e/test.txt
        /work/build/docker/sparky_siem /tmp/e2e/test.txt localhost:9092 my-topic &
        P=$!
        sleep 3
        echo "first appended line" >> /tmp/e2e/test.txt
        sleep 2
        kill -INT $P; wait $P; echo "exit=$?"
    '
```

Verify the count at the broker rather than by eye: end offset should advance by
exactly the number of messages expected (`INIT`, `INIT - FILE OPEN`, one
`MODIFY` per appended line, `CLOSE`).

### Behaviours worth checking by hand

```sh
echo 'one line' >> /tmp/e2e/test.txt              # exactly one MODIFY
printf 'he said "hello" \\ tab\there\n' >> ...    # still valid JSON
mv /tmp/e2e/test.txt /tmp/e2e/test.txt.1; : > /tmp/e2e/test.txt   # ROTATE + REWATCH
chmod 000 /tmp/e2e/test.txt                       # one ERROR, no CPU spin
kill -INT <pid>                                   # CLOSE, exit 0
```

While a read is failing, confirm the process is not spinning:

```sh
awk '{print "utime="$14" stime="$15}' /proc/<pid>/stat   # both should stay 0
```

### Dead broker

```sh
docker run --rm -v "$PWD:/work" -w /work sparky-siem-test:latest bash -c '
    mkdir -p /tmp/dead && : > /tmp/dead/f.txt
    /work/build/docker/sparky_siem /tmp/dead/f.txt 127.0.0.1:59998 my-topic 2>&1 |
        grep -v "^%"
'
```

Reports `Kafka flush did not complete (...), N message(s) still outstanding` at
shutdown. The per-message `Kafka delivery failed` log only fires once librdkafka
gives up, which at the default `message.timeout.ms` of 300000 takes five
minutes; the unit test covers that path with a 1500 ms override instead.

### rand_data_gen smoke test

```sh
docker run --rm --network container:ctest_broker \
    -v "$PWD:/work" -w /work sparky-siem-test:latest bash -c '
        mkdir -p /tmp/rdg && cd /tmp/rdg
        cp /work/rand_data_gen/rand_data_gen.c .
        gcc -O2 -o gen rand_data_gen.c
        : > file.txt
        /work/build/docker/sparky_siem /tmp/rdg/file.txt localhost:9092 rdg-smoke &
        P=$!
        sleep 3
        ./gen 5                       # argument x 100 lines; writes ./file.txt
        sleep 6
        echo "generated: $(wc -l < file.txt)"
        kill -INT $P; wait $P
    '
```

Then confirm one message per line, with no duplicates:

```sh
docker exec ctest_broker bash -c '
    grep -c "\"type\": \"MODIFY\"" /tmp/rdg.txt
    grep "\"type\": \"MODIFY\"" /tmp/rdg.txt |
        sed "s/.*\"message\": \"//; s/\", \"type.*//" | sort -u | wc -l
'
```

Both numbers should equal the generated line count (500 for `./gen 5`).

### Tear down

```sh
cd docker_stuff && docker compose down -v && cd ..
```

---

## 7. Gotchas that cost real time

* **inotify does not fire on macOS Docker bind mounts.** Anything under the
  mounted repo (`/work`) will never generate events. Every test and manual check
  must write to a path on the container's own filesystem, which is why the
  tests use `mkdtemp` under `/tmp` and the scripts above use `/tmp/e2e`.
* **The test container runs as non-root** (`sparky`, uid 1000). Under root,
  `chmod 000` does not deny reads, and `FileMonitorFailures.AnUnreadableFile...`
  would silently `GTEST_SKIP` instead of testing anything.
* **`ctest` and pipes hide progress.** See section 3.
* **The Kafka tests need no broker.** They use librdkafka's in-process mock
  cluster (`test.mock.num.brokers=1`), which also auto-creates the topic. If a
  librdkafka build lacked it the tests would skip rather than fail, so check for
  `SKIPPED` in the output when in doubt - a correct run reports none.
* **`docker build -q` suppresses all output,** which makes a slow first image
  build look like a hang.
* **The test image has no `pkg-config`,** so `pkg_check_modules(rdkafka++)` in
  `CMakeLists.txt` finds nothing and the plain `-lrdkafka++ -lrdkafka` fallback
  is what actually links. That works - the suite passes and messages reach a real
  broker - but the primary discovery path is therefore not exercised by
  `run_tests.sh`. Adding `pkg-config` to `docker_stuff/Dockerfile.test` would
  cover it.

---

## 8. Regenerating the files in this directory

```sh
python3 misc/extract_session.py \
    ~/.claude/projects/-Users-jamster-CLionProjects-SparkySIEM/<session-id>.jsonl \
    misc/chat-transcript.md misc/terminal-commands.md
```
