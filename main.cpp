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
    FileMonitor monitor("/tmp/test.txt", "sparkysiem_kafka:29092", "sparky-changes");
    monitor.monitor();
    return 0;
}
