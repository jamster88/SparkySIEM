# SparkySIEM — Build and Test Commands: the group monitor

Every component involved in the group monitor (`FilesMonitor`), how to build it and how to
test it. All commands are run from the repository root unless stated otherwise.

This file covers the group-monitor session of 2026-08-26 and reflects the suite as it
stands now: **64 tests from 16 test suites**. The single-file monitor's own reference,
`misc/build-and-test-commands.md`, is left exactly as that session wrote it and still
describes the suite at its earlier size of 45 tests; where the two disagree on totals,
this file is the current one.

**Platform note:** the monitors use `inotify`, which is Linux only. On macOS or Windows,
build and test through the container workflow below. On Linux, `make` works directly.

---

## 1. SparkySIEM — the forwarder

**Sources:** `main.cpp`, `FilesMonitor.{h,cpp}`, `FileMonitor.{h,cpp}`,
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

> The first image build installs the whole toolchain in one `apt-get` layer. On a slow
> mirror that takes around 20 minutes, and `run_tests.sh` builds with `-q`, so it looks
> identical to a hang. Use `docker build --progress=plain -f Dockerfile.test -t
> sparkysiem-tests .` if you need to see where it is. Later runs reuse the cached image.

### Run the group monitor

```sh
./build/SparkySIEM <broker> <topic> <path> [path...]

# examples
./build/SparkySIEM localhost:9092 my-topic /var/log                       # a directory
./build/SparkySIEM localhost:9092 my-topic /var/log /etc/myapp/audit.log  # and a file
```

Each path may be a file or a directory, and the two kinds can be mixed freely. Directories
are rescanned once a second, so files created later are picked up and deleted files have
their monitors shut down. A path that does not exist yet is not an error; it is picked up
when it appears. Ctrl-C (SIGINT) or SIGTERM stops everything cleanly and flushes.

### Verify it built cleanly (no warnings, both binaries)

```sh
docker run --rm -v "$PWD":/work:ro -w /work sparkysiem-tests bash -c \
    'cp -r /work /tmp/s && cd /tmp/s && make BUILD_DIR=/tmp/b 2>&1 | grep -iE "warn|error"; \
     echo "APP BUILD EXIT: ${PIPESTATUS[0]}"; ls -l /tmp/b/SparkySIEM; \
     make test BUILD_DIR=/tmp/b 2>&1 | grep -iE "warning|error:"; echo "TEST BUILD clean"'
```

---

## 2. Unit tests

**Sources:** `tests/test_files_monitor.cpp` (the group monitor, 30 tests),
`tests/test_file_monitor.cpp`, `tests/test_message_format.cpp`, `tests/TestSupport.h`,
`tests/FakeSink.h`
**Dependencies:** `libgtest-dev`, `nlohmann-json3-dev`, plus the app's dependencies
**Kafka is NOT required** — the tests publish into an in-memory `MessageSink`.

### Run everything (any host, containerised)

```sh
./run_tests.sh
```

Mounts the repository **read only** and builds into a scratch path inside the container,
so it never leaves artifacts in the working tree.

### Run only the group monitor's tests

```sh
./run_tests.sh --gtest_filter='FilesMonitor*.*'
./run_tests.sh --gtest_filter='FilesMonitorShutdown.*'
./run_tests.sh --gtest_list_tests
```

### Run on a Linux host, directly

```sh
make test
make test GTEST_ARGS="--gtest_filter=FilesMonitorCleanup.*"
```

### Check for flaky, timing-sensitive tests

```sh
docker run --rm -v "$PWD":/work:ro -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/sparky-build \
    GTEST_ARGS="--gtest_repeat=6 --gtest_shuffle --gtest_brief=1"
```

Expected: `64 tests from 16 test suites ran. [ PASSED ] 64 tests.` on each repeat.
Several of the group-monitor tests assert on timing (shutdown latency, a scan interval cut
short), so this is the run that matters when changing the scan or shutdown paths.

### Group-monitor test suites

| Suite | What it covers |
| --- | --- |
| `FilesMonitorConstruction.*` | Null sink, paths that do not exist, an empty path list, non-copyable |
| `FilesMonitorFiles.*` | Several files at once, files and directories mixed, only-new-lines per file, each file's messages under its own `filePath`, no double monitoring when a file and its directory are both listed, a file listed before it exists |
| `FilesMonitorDirectories.*` | Directory contents, files created after start, no recursion, non-regular entries (FIFOs) skipped, file names containing quotes, a busy directory |
| `FilesMonitorFailures.*` | One unwatchable file among many, reported once, reported again after it comes back, a sink that fails every publish |
| `FilesMonitorCleanup.*` | Deleted files dropped and closed, a deleted file's shutdown not blocking the group, recreation at the same path, rotation inside a watched directory |
| `FilesMonitorShutdown.*` | Prompt destructor, idempotent stop, nothing started after `stop()`, a long scan interval cut short |

The single-file monitor's suites (`FileMonitor*.*`) and the message-format suites
(`EscapeJson.*`, `FormatMessage.*`, `CurrentTimestamp.*`) are documented in
`misc/build-and-test-commands.md`.

> Three tests depend on file permissions and skip themselves when run as root, because
> root ignores them: `FileMonitorFailures.AnUnreadableFileDoesNotFloodTheSink`,
> `FilesMonitorFailures.KeepsMonitoringTheOtherFilesWhenOneCannotBeWatched` and
> `FilesMonitorFailures.ReportsAnUnwatchableFileOnlyOnce`. `Dockerfile.test` runs the
> suite as the non-root user `sparky`, so they actually execute.

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
docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh \
    --create --topic my-topic --bootstrap-server ctest_broker:29092

docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh \
    --list --bootstrap-server ctest_broker:29092

docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic my-topic --from-beginning \
    --bootstrap-server ctest_broker:29092 --timeout-ms 10000

docker exec ctest_broker /opt/kafka/bin/kafka-get-offsets.sh \
    --bootstrap-server ctest_broker:29092 --topic my-topic
```

Addresses: `localhost:9092` from the host, `ctest_broker:29092` from another container on
the `docker_stuff_default` network.

---

## 4. End-to-end check of the group monitor against a real broker

Confirms the forwarder really delivers, with several files at once. Not part of the unit
suite.

```sh
# 1. broker up
cd docker_stuff && docker compose up -d && cd ..

# 2. build and run the forwarder inside the Kafka network, on a directory AND a file
docker build -f Dockerfile.test -t sparkysiem-tests .
docker run --rm --network docker_stuff_default -v "$PWD":/work:ro sparkysiem-tests bash -c '
    cp -r /work /tmp/src && cd /tmp/src && make BUILD_DIR=/tmp/b -s
    gcc -Wall -o /tmp/b/rand_data_gen rand_data_gen/rand_data_gen.c
    mkdir -p /tmp/logs /tmp/solo
    printf "line A\n"     > /tmp/logs/app.log
    printf "other file\n" > /tmp/logs/second.log
    printf "solo start\n" > /tmp/solo/explicit.log

    /tmp/b/SparkySIEM ctest_broker:29092 sparky-group /tmp/logs /tmp/solo/explicit.log &
    APP=$!; sleep 3

    printf "line B\n"                    >> /tmp/logs/app.log;        sleep 1
    printf "second change\n"             >> /tmp/logs/second.log;     sleep 1
    printf "he said \"hi\" \\ done\n"    >> /tmp/logs/app.log;        sleep 1
    printf "quoted name\n"                > "/tmp/logs/we\"ird name.log"; sleep 2
    printf "new file created\n"           > /tmp/logs/third.log;      sleep 2

    cd /tmp && /tmp/b/rand_data_gen 10 2>/dev/null   # 1000 lines into ./file.txt
    cat /tmp/file.txt >> /tmp/logs/app.log;          sleep 3

    rm /tmp/logs/second.log;                         sleep 2   # deletion
    mv /tmp/logs/app.log /tmp/logs/app.log.1                   # rotation
    printf "after rotation\n" > /tmp/logs/app.log;   sleep 3

    kill -TERM $APP; wait $APP; md5sum /tmp/file.txt'

# 3. inspect what landed on the topic
docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh \
    --topic sparky-group --from-beginning \
    --bootstrap-server ctest_broker:29092 --timeout-ms 10000 > /tmp/topic.jsonl
```

### Check the result rather than eyeballing it

```sh
python3 - /tmp/topic.jsonl <<'PY'
import json, sys, collections, hashlib
msgs = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]   # throws on bad JSON
print("total:", len(msgs))
print("types:", dict(collections.Counter(m['type'] for m in msgs)))
for f in sorted({m['filePath'] for m in msgs}):
    lines = [m['message'] for m in msgs if m['filePath'] == f and m['type'] == 'MODIFY']
    life  = [m['type']    for m in msgs if m['filePath'] == f and m['type'] != 'MODIFY']
    dups  = sum(c - 1 for c in collections.Counter(lines).values() if c > 1)
    print(f"  {f}: {len(lines)} lines, {dups} duplicates, {life}")
app = [m['message'] for m in msgs if m['filePath'] == '/tmp/logs/app.log' and m['type'] == 'MODIFY']
print("bulk md5:", hashlib.md5(("\n".join(app[3:-1]) + "\n").encode()).hexdigest())
PY
```

Expect: every message parsing, each line exactly once per file, the bulk md5 equal to the
`md5sum /tmp/file.txt` printed by the run, a `ROTATED` for `app.log` followed by
`after rotation`, and a `CLOSE` for every file including the deleted and the late one.
`app.log.1` republishing its contents is the documented rotation-inside-a-directory
limitation, not a failure.

---

## 5. Synthetic log generator (`rand_data_gen/`)

```sh
cd rand_data_gen
gcc -Wall -o rand_data_gen rand_data_gen.c
./rand_data_gen 10      # writes file.txt with 10 * 100 = 1000 lines
cd ..
```

The argument is multiplied by `CNT_SCALE` (100); with no argument it writes 1000 lines.
Output always goes to `file.txt` in the current directory. `srand()` is never called, so
the data is identical on every run — which is what makes the md5 comparison above work.

---

## 6. Mutation testing (verifies the group-monitor tests have teeth)

Reintroduce a defect into a copy of the source and confirm the suite fails. Never modifies
the working tree.

```sh
WORK=$(mktemp -d)
rsync -a --exclude .git --exclude build ./ "$WORK/"

# example: undo the fix that keeps a deleted file's shutdown off the monitor lock
python3 - "$WORK" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1]) / "FilesMonitor.cpp"
s = p.read_text()
s = s.replace("""            it->second.monitor->stop();
            finished.push_back(std::move(it->second));""",
"""            it->second.monitor->stop();
            if (it->second.worker.joinable()) { it->second.worker.join(); }
            finished.push_back(std::move(it->second));""")
p.write_text(s)
PY

docker run --rm -v "$WORK":/work:ro -w /work sparkysiem-tests \
    make test BUILD_DIR=/tmp/b GTEST_ARGS="--gtest_filter=FilesMonitorCleanup.*"
rm -rf "$WORK"
```

Expected: the run **fails**. A mutation that passes means the suite is missing a test.

Mutations checked during the group-monitor session, all caught:

| Defect reintroduced | Caught by |
| --- | --- |
| Monitors created but `monitor()` never called | 11 `FilesMonitor*` tests |
| Deleted file joined while `monitorMutex` is held | `FilesMonitorCleanup.ADeletedFilesShutdownDoesNotBlockTheGroup` |
| Reported-failure set never pruned | `FilesMonitorFailures.ReportsAnUnwatchableFileAgainAfterItComesBack` |
| `stop()` not waking the scanning thread | `FilesMonitorShutdown.StopCutsALongScanIntervalShort` (30 s instead of < 5 s) |

---

## 7. How many files can one forwarder follow?

Each monitored file costs one thread and one `inotify` instance, so
`fs.inotify.max_user_instances` (commonly 128) is the ceiling. To measure the behaviour at
the limit, lower it and point the monitor at more files than that. The sysctl is global to
the Docker VM, so restore it in the same command.

```sh
docker run --rm --privileged --user root -v "$PWD":/work:ro sparkysiem-tests bash -c '
    ORIGINAL=$(cat /proc/sys/fs/inotify/max_user_instances)
    sysctl -q -w fs.inotify.max_user_instances=20
    mkdir -p /tmp/many
    for i in $(seq 1 40); do printf "line %s\n" "$i" > "/tmp/many/f$i.log"; done
    cp -r /work /tmp/src && cd /tmp/src && make BUILD_DIR=/tmp/b -s
    chown -R sparky /tmp/many
    su sparky -c "/tmp/b/SparkySIEM ctest_broker:29092 probe /tmp/many" 2>/tmp/errs.txt &
    APP=$!; sleep 4; kill -TERM $APP; wait $APP 2>/dev/null
    echo "files reported unwatchable: $(grep -c "Failed to initialize inotify" /tmp/errs.txt)"
    echo "distinct paths named:       $(grep "Failed to monitor" /tmp/errs.txt | sort -u | wc -l)"
    sysctl -q -w fs.inotify.max_user_instances="$ORIGINAL"
    echo "limit restored to $(cat /proc/sys/fs/inotify/max_user_instances)"'
```

There is no broker on this container's network, so librdkafka also writes connection
errors to the same stream; that is why the counts above grep for the inotify failures
rather than counting every line.

Measured with the limit at 20 and 40 files present: 20 files monitored, 20 distinct
reports (one per file, not one per scan), no retry storm, and the 20 that fit published
normally.

---

## 8. Quick reference

| Goal | Command |
| --- | --- |
| Build the forwarder | `make` |
| Monitor a directory | `./build/SparkySIEM localhost:9092 my-topic /var/log` |
| Monitor a directory and a file | `./build/SparkySIEM localhost:9092 my-topic /var/log /etc/app/audit.log` |
| Run all tests (macOS) | `./run_tests.sh` |
| Run all tests (Linux) | `make test` |
| Run only the group monitor's tests | `./run_tests.sh --gtest_filter='FilesMonitor*.*'` |
| Check for flakiness | `./run_tests.sh --gtest_repeat=6 --gtest_shuffle --gtest_brief=1` |
| Start Kafka | `cd docker_stuff && docker compose up -d` |
| Read a topic | `docker exec ctest_broker /opt/kafka/bin/kafka-console-consumer.sh --topic <t> --from-beginning --bootstrap-server ctest_broker:29092 --timeout-ms 10000` |
| Stop Kafka | `cd docker_stuff && docker compose down -v` |
| Generate test data | `cd rand_data_gen && gcc -Wall -o rand_data_gen rand_data_gen.c && ./rand_data_gen 10` |
| Clean build output | `make clean` |
