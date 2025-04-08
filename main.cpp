//
// Created by Jamster on 4/4/25.
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
    FileMonitor monitor("/path/to/your/file.txt", "localhost:9092", "your_kafka_topic");
    monitor.monitor();
    return 0;
}
