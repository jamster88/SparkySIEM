//
//
// Created by Jamster on 4/4/25.
//
// Entry point for the single-file monitor. Configure the three arguments below:
//   filePath     — file to watch for modifications (inotify is Linux-only)
//   kafkaBroker  — broker address; use "sparkysiem_kafka:29092" inside Docker,
//                  or "localhost:9092" when Kafka runs on the host
//   kafkaTopic   — target topic for produced events
//
#include <iostream>
#include <fstream>
#include <string>
#include <librdkafka/rdkafkacpp.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <errno.h>
#include "FileMonitor.h"

int main() {
    // Defaults tuned for Docker Compose (Kafka on the compose network).
    // Change kafkaBroker to "localhost:9092" if running outside containers.
    FileMonitor monitor("/tmp/test.txt", "sparkysiem_kafka:29092", "sparky-changes");
    monitor.monitor();
    return 0;
}
