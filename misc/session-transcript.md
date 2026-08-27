# SparkySIEM — Session Transcript

**Date:** 2026-08-18 / 2026-08-19
**Branch:** `jam_ind_file_mon_claude`
**Host:** macOS (Darwin 27.0.0, arm64), Apple clang 17
**Task:** Verify the file change monitor works as expected, then write unit tests for it.

---

## 1. User request

> This is supposed to be a file change monitor that detects changes in the specified file
> or files and sends those changes to a specified Kafka destination. The `docker_stuff`
> folder contains the necessary files and configurations to set up a kafka environment for
> testing purposes. The `rand_data_gen` folder contains the code for generating random data
> for testing purposes. Please verify that the file change monitor works as expected, then
> write unit tests for it.

---

## 2. Environment findings

The code is Linux-only (`inotify`), but the host is macOS arm64. Docker was available, so
all building, running and testing was done inside Linux containers.

- `/usr/include/sys/inotify.h` — absent (expected on macOS)
- `librdkafka` headers present via Homebrew, but the app cannot build natively (no inotify)
- The committed `SparkySIEM` binary is `ELF 64-bit LSB pie executable, x86-64` — a stale
  Linux build of the old code, produced on a different machine
- Docker running; `ctest_broker` (apache/kafka) from `docker_stuff/compose.yaml` available
- No test scaffolding existed on any branch (`main`, `jam_grp_file_mon`, `jam_ind_file_mon`,
  `origin/jam_ind_file_mon_claude`) — clean slate

A build image was created (Ubuntu 24.04 + `g++`, `librdkafka-dev`, `libgtest-dev`).

---

## 3. Verification results — it did not work as expected

### 3.1 Compile failure (blocker)

`FilesMonitor.cpp:110` called `std::make_unique<FileMonitor>(filePath, topic)` — two
arguments — while the constructor required three (`filePath`, `kafkaBroker`, `kafkaTopic`).

```
error: no matching function for call to 'FileMonitor::FileMonitor(
        const std::__cxx11::basic_string<char>&, std::__cxx11::basic_string<char>&)'
note: candidate expects 3 arguments, 2 provided
```

`main.cpp` + `FileMonitor.cpp` compiled cleanly; the multi-file path had never been built.

### 3.2 `FilesMonitor` never ran its monitors

Even with the constructor fixed, `handleFile()` constructed `FileMonitor` objects but never
called `monitor()` on any of them. It would have created inotify watches and published nothing.

### 3.3 Whole file resent on every change (confirmed on live Kafka)

Monitor started on a file containing `line A`, then `line B` and `line C` appended one at a
time. Consumed from topic `sparky-verify`:

```
"message": "line A", "type": "MODIFY"     <- 1st append
"message": "line B", "type": "MODIFY"
"message": "line A", "type": "MODIFY"     <- 2nd append, line A AGAIN
"message": "line B", "type": "MODIFY"
"message": "line C", "type": "MODIFY"
"message": "line A", "type": "MODIFY"     <- 3rd append, line A a THIRD time
"message": "line B", "type": "MODIFY"
"message": "line C", "type": "MODIFY"
```

The monitor re-read the file from the beginning on every event. Traffic grows O(n²) with
file size — unusable for a real log file.

### 3.4 Invalid JSON (confirmed on live Kafka)

A line containing a quote and a backslash produced an unparseable message:

```
{"timestamp": "...", "message": "he said "hi" \ done", "type": "MODIFY"}
                                          ^^^^    ^  unescaped
```

### 3.5 Unreadable file → infinite busy loop (confirmed on live Kafka)

`FileMonitor.cpp:196` used `continue` inside `for (int i = 0; i < length;)`, skipping the
`i += sizeof(...) + event->len` increment. With the file made unreadable to the monitor
process, in roughly four seconds:

```
sparky-perm:0:1799740      <- 1,799,740 ERROR messages pushed to Kafka
1800042 /tmp/perm.err      <- 1.8 million stderr lines
```

CPU pegged; the loop never exits.

### 3.6 Log rotation silently killed it (confirmed on live Kafka)

The inotify watch follows the inode. After `mv app.log app.log.1` and creating a fresh file
at the same path, nothing further was ever captured — and no error was reported:

```
"message": "orig1", "type": "MODIFY"
"message": "orig2", "type": "MODIFY"
   (newfile1 and newfile2 never appear)
```

### 3.7 Static defects

- No graceful shutdown: `while (true)` made the `CLOSE` message and `producer->flush()` at
  `FileMonitor.cpp:210-211` unreachable dead code; buffered messages could be lost at exit
- `FilesMonitor::stopMonitoring` was a plain `bool` written and read across threads — data race
- Rule of three violated: copyable class owning a raw `RdKafka::Producer*` and two fds
- `conf` leaked when the constructor threw; `inotifyFd` leaked when `inotify_add_watch` failed
- No delivery-report callback, so broker-side send failures were silently dropped
- `std::localtime` is not thread-safe, in a design that is multi-threaded
- `int length = read(...)` should be `ssize_t`
- `main.cpp` hardcoded `/home/jamster/Repos/SparkySIEM/test.txt`

---

## 4. Decision point

Given that a test suite needs a testable seam and the code did not compile, the user was
asked how to proceed. Answers:

- **Test scope:** *Fix defects, then test* — fix the bugs, add a sink seam, deliver a green suite
- **Framework:** *GoogleTest in Docker* — required, since inotify does not exist on macOS

---

## 5. Fixes applied

**New files**

| File | Role |
| --- | --- |
| `MessageSink.h` | Destination interface — the seam that makes the monitors testable |
| `KafkaSink.{h,cpp}` | librdkafka implementation, with a delivery-report callback |
| `MessageFormat.{h,cpp}` | Timestamp + JSON construction, with proper escaping |

**`FileMonitor`**

- Tracks a byte offset; publishes only newly appended lines
- Buffers partial lines until the terminating newline arrives (never forwards half a record)
- Detects truncation (`TRUNCATED`) and rotation (`ROTATED`) — the parent directory is watched
  so a replacement file at the same path is picked up and followed
- Escapes every JSON value
- `poll()` on the inotify fd plus an `eventfd`, replacing `while(true)` + blocking `read()`:
  `stop()` is prompt, `CLOSE` + flush actually run, and a failed open cannot busy-loop
- Open failures reported once, not once per event
- Non-copyable; no leaks on constructor error paths; `localtime_r`

**`FilesMonitor`**

- Compiles (broker threaded through), and actually runs each `FileMonitor` on its own thread
- `std::atomic<bool>` stop flag, condition-variable sleep so `stop()` cuts the interval short
- Stops and joins every monitor thread on shutdown; cleans up monitors for deleted files
- One unwatchable file no longer prevents the others from being monitored

**`main.cpp`** — takes `<broker> <topic> <path>...` from argv, signal-based clean shutdown.

---

## 6. Test suite

**45 GoogleTest tests, all passing**, stable across 3 shuffled repeat runs.

| File | Coverage |
| --- | --- |
| `tests/test_message_format.cpp` | JSON escaping (quotes, backslashes, control chars, UTF-8), message fields, round-trip through a real JSON parser, timestamp format |
| `tests/test_file_monitor.cpp` | Construction/throwing, INIT messages, existing content, incremental reads, partial lines, empty lines, CRLF, 500-line ordering, JSON validity, truncation, rotation, unreadable files, publish failures, shutdown |
| `tests/test_files_monitor.cpp` | Multiple files, directory monitoring, late-created files, deleted files, non-recursion, null sink, prompt shutdown |
| `tests/TestSupport.h` | `TempDir`, `waitFor` polling, `MonitorRunner`, JSON accessors |
| `tests/FakeSink.h` | In-memory `FakeSink` and `ThrowingSink` |

### Mutation testing

A green suite proves nothing unless it catches the original bugs, so each defect was
reintroduced one at a time:

| Mutation | Result |
| --- | --- |
| Whole-file resend (reset offset each read) | **KILLED** — `PublishesAnAppendedLineExactlyOnce`, `RepeatedAppendsNeverResendEarlierLines` |
| No JSON escaping | **KILLED** — 6 tests including `ContentWithQuotesAndBackslashesStaysValidJson` |
| No rotation handling | **KILLED** — `FollowsANewFileThatReplacesTheWatchedPath` |
| Error reported per-event instead of once | **SURVIVED** at first |

The surviving mutation exposed a weak assertion: the flood test allowed ≤2 error messages,
and inotify had coalesced the rapid appends into one event. The appends were spaced out and
the assertion tightened to exactly 1 — the mutation was then **KILLED**.

---

## 7. End-to-end verification of the fixed code

Fixed binary run against the live broker, monitoring a *directory*, with the same scenario
that broke the original. Consumed from topic `sparky-fixed`:

```
app.log     INIT / INIT - FILE OPEN / "line A"
second.log  INIT / INIT - FILE OPEN / "other file"
app.log     "line B"                                   <- once
app.log     "line C"                                   <- once
app.log     "he said \"hi\" \\ done"                   <- valid JSON
app.log     ROTATED
app.log     "after rotation"                           <- followed the new file
third.log   INIT / "new file created"                  <- new file in watched dir
app.log, app.log.1, second.log, third.log   CLOSE      <- clean shutdown, all four
```

Every defect confirmed fixed.

---

## 8. Flagged, not fixed

- **Rotation inside a monitored directory double-publishes.** The rotated `app.log.1` looks
  like a brand new file to the directory scan, so its contents go out a second time (visible
  in the run above). Fixing it needs per-inode bookkeeping of consumed content, in the style
  of Splunk's fishbucket — a feature, not a bug fix. Documented under Known Limitations.
- Existing file content is published once at startup, so a restart replays current contents.
- Directory monitoring is not recursive.
- `ConfigReader.cpp` is still empty.
- The committed `SparkySIEM` ELF binary is a stale build of the old code; `make` now outputs
  to `build/`.
- The Kafka broker was running at session start, was removed mid-session by something pruning
  Docker state, and was restarted via `docker compose up -d`.

---

## 9. Final state

```
 M FileMonitor.cpp      M FilesMonitor.cpp     M README.md      M main.cpp
 M FileMonitor.h        M FilesMonitor.h       M cpp_compiler_commands.txt
?? .gitignore  ?? Dockerfile.test  ?? KafkaSink.{h,cpp}  ?? Makefile
?? MessageFormat.{h,cpp}  ?? MessageSink.h  ?? run_tests.sh  ?? tests/  ?? misc/

7 files changed, 777 insertions(+), 416 deletions(-)   (tracked files only)
```

Nothing was committed — the working tree holds the changes for review.
