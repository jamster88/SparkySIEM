# SparkySIEM

Spawns from some scratch work in: [text](https://github.com/jamster88/kafka_expts)

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


## How it works

```
   paths on the command line
            |
            v
     FilesMonitor            one scanning thread; rescans the paths every second,
            |                starting and stopping per-file monitors as files
            |                appear and disappear
            v
      FileMonitor  x N       one per file, each on its own thread; watches the file
            |                with inotify and publishes the lines appended to it
            v
      MessageSink            interface: what to do with a finished message
            |
            +--> KafkaSink   production, wraps librdkafka
            +--> FakeSink    tests, keeps messages in memory (no broker needed)
```

`MessageFormat` builds the JSON and the timestamps for all of the above. Splitting the
destination out behind `MessageSink` is what lets the monitors be tested without Kafka.


## Requirements

* Linux, for `inotify` and `eventfd`. The container workflow below covers other hosts.
* A C++17 compiler and `make`
* `librdkafka-dev` to build the forwarder
* `libgtest-dev` and `nlohmann-json3-dev` to build the tests
* Docker, to run the test suite off Linux or to bring up the Kafka broker

On Debian or Ubuntu:

```sh
sudo apt-get install g++ make librdkafka-dev libgtest-dev nlohmann-json3-dev
```

`Dockerfile.test` installs exactly this set, so it doubles as the reference for what is
needed.


## Building and running

inotify is Linux only, so this builds and runs on Linux. From a macOS or Windows host, use
the container workflow below.

```sh
make                 # builds build/SparkySIEM
./build/SparkySIEM <broker> <topic> <path> [path...]
```

For example, against the broker in `docker_stuff/`:

```sh
cd docker_stuff && docker compose up -d && cd ..
./build/SparkySIEM localhost:9092 my-topic /var/log/app.log
```

Each path may be a file or a directory. Directories are scanned once a second, so files
created later are picked up, and files that disappear have their monitor shut down.
Ctrl-C stops everything cleanly.


## Testing

```sh
./run_tests.sh                                        # runs the suite in a Linux container
./run_tests.sh --gtest_filter='FileMonitorRotation.*'  # or a subset
make test                                             # on a Linux host, directly
```

`run_tests.sh` mounts the repository read only and builds inside the container, so it
never leaves artifacts in the working tree. The tests use an in-memory `MessageSink`
instead of a broker, so no Kafka instance is needed to run them.

`misc/build-and-test-commands.md` has the fuller reference: per-component build and test
commands, the Kafka topic commands, an end-to-end recipe against a real broker, and the
mutation-testing procedure used to check that the suite actually catches regressions.


## Message format

Every message is a JSON object. All values are escaped, so log lines containing quotes
or backslashes still produce parseable messages.

```json
{"timestamp": "2026-08-19 04:24:02.576", "filePath": "/tmp/logs/app.log", "kafkaTopic": "my-topic", "message": "he said \"hi\"", "type": "MODIFY"}
```

| type | meaning |
| --- | --- |
| `INIT` | monitoring started for this file |
| `INIT - FILE OPEN` | the file was opened successfully |
| `ERROR - FILE OPEN` | the file could not be read; reported once, not once per event |
| `MODIFY` | a line of file content |
| `TRUNCATED` | the file shrank, so reading restarted from the beginning |
| `ROTATED` | a new file replaced the watched path; the monitor followed it |
| `CLOSE` | monitoring stopped and the sink was flushed |

A file's existing content is published once when monitoring starts, and only newly
appended lines are published after that. A line is held back until its terminating
newline arrives, so a partially written record is never forwarded in halves.


## Layout

| file | role |
| --- | --- |
| `main.cpp` | argument parsing and signal-based shutdown |
| `FileMonitor.*` | watches one file with inotify and publishes its changes |
| `FilesMonitor.*` | watches a list of files and directories, one FileMonitor per file |
| `MessageSink.h` | destination interface; lets the monitors be tested without a broker |
| `KafkaSink.*` | librdkafka implementation of that interface |
| `MessageFormat.*` | timestamp and JSON message construction |
| `ConfigReader.cpp` | placeholder for config file support; not implemented, not compiled |
| `Makefile` | builds the forwarder and the tests |
| `Dockerfile.test` | Linux image with every build and test dependency |
| `run_tests.sh` | runs the suite in that image, for non-Linux hosts |
| `tests/` | GoogleTest suite and its helpers |
| `docker_stuff/` | Kafka broker for local testing |
| `rand_data_gen/` | generator for synthetic log data |
| `misc/` | session notes: verification transcript, commands run, build and test reference |

Two leftovers worth knowing about: the `SparkySIEM` binary committed at the repository
root is a stale x86-64 Linux build of the pre-fix code, kept from an earlier commit, and
`make` now writes to `build/` instead. `.vscode/tasks.json` still describes a single-file
`gcc` build that predates the Makefile, so it will not build this project as it stands.


## Known limitations

* Directory monitoring is not recursive; nested directories are ignored.
* When a file is rotated inside a monitored *directory*, the rotated copy
  (`app.log.1`) looks like a brand new file to the directory scan, so its contents are
  published a second time. Avoiding this needs per-inode bookkeeping of what has already
  been consumed, in the style of Splunk's fishbucket.
* There is no persistent state, so restarting republishes the current contents of every
  monitored file.
* `ConfigReader.cpp` is still empty; broker, topic and paths come from the command line.


## TO-DO

* Persistent read positions so a restart resumes instead of replaying
* Config file support (`ConfigReader`)
* Recursive directory monitoring
* TLS to the broker
* Feature work planning
