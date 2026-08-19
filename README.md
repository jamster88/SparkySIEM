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

Each message is a single-line JSON object, so `from_json` and friends can read it directly:

```json
{"timestamp": "2026-08-18 12:34:56.789", "filePath": "/var/log/app.log", "kafkaTopic": "my-topic", "message": "eat 42 fish", "type": "MODIFY"}
```

`type` distinguishes file content (`MODIFY`) from lifecycle events: `INIT` and `INIT - FILE OPEN` at startup, `TRUNCATE` when the file shrinks, `ROTATE` and `REWATCH` when the file is renamed or replaced, `ERROR - FILE OPEN` / `ERROR - FILE STAT` when it cannot be read, and `CLOSE` on a clean shutdown.


## Behaviour

* Only content appended **after** the monitor starts is forwarded, so restarting it does not replay a file that has already been shipped.
* A line is only sent once it is complete; a partially written line is held until its newline arrives.
* Log rotation is followed: when the file is renamed, deleted, or replaced, the monitor reports it and picks up the new file at that path.
* Delivery is asynchronous, so failures are reported through librdkafka's delivery reports rather than assumed successful.
* `SIGINT`/`SIGTERM` shut down cleanly, sending a `CLOSE` message and flushing anything still queued.

Known limitation: a file truncated in place and immediately rewritten to a *larger* size than the previous read offset cannot be distinguished from an append, so the intervening bytes are read as if appended. Rotation by rename or replacement is detected reliably.


## Building and testing

The monitor uses **inotify**, so it builds on Linux only. From a macOS or Windows host, build and test in the provided container:

```sh
./docker_stuff/run_tests.sh                              # build + run all tests
./docker_stuff/run_tests.sh --gtest_filter='FileMonitorTailing.*'   # a subset
```

That is 44 GoogleTest cases covering message formatting, incremental tailing, truncation, rotation, read failures and shutdown. None of them need a Kafka broker: the monitor tests drive it through a recording `MessageSink`, and the `KafkaSink` tests use librdkafka's in-process mock cluster. The container deliberately runs as a non-root user, because `chmod 000` does not deny reads to root and the unreadable-file test would skip itself instead of testing anything.

On Linux you can also build directly, given `librdkafka-dev` and `libgtest-dev`:

```sh
cmake -S . -B build && cmake --build build
cd build && ctest --output-on-failure
./sparky_siem /var/log/app.log localhost:9092 my-topic   # file, broker, topic
```

A local Kafka for manual testing lives in `docker_stuff/` (`docker compose up -d`, see `docker_stuff/commands`), and `rand_data_gen/` generates filler data to append.

One trap worth knowing before testing by hand: **inotify does not fire for files on a Docker bind mount**, so editing a file in the mounted repo from a macOS or Windows host produces no events at all. Write to a path on the container's own filesystem instead (the tests use temporary directories under `/tmp` for exactly this reason).

`misc/build-and-test-commands.md` has the longer version of all of this, including the manual end-to-end checks against a real broker.


## TO-DO

* ~~Finish the barebones version~~
* ~~Write tests~~
* Watch multiple files, and directories
* Feature work planning
* *flesh this out*
