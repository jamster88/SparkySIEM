//
// Created by Jamster on 4/4/25.
//
// Entry point: point SparkySIEM at one or more files or directories and it forwards
// their changes to a Kafka topic until interrupted.
//
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "FilesMonitor.h"

namespace {
/// Set by the signal handler; polled by main so shutdown work stays off the handler.
volatile std::sig_atomic_t gShutdownRequested = 0;

extern "C" void handleSignal(int) {
    gShutdownRequested = 1;
}

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " <broker> <topic> <path> [path...]\n"
              << "  broker  Kafka bootstrap server, e.g. localhost:9092\n"
              << "  topic   Kafka topic to publish to, e.g. my-topic\n"
              << "  path    File or directory to monitor. May be repeated.\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        printUsage(argv[0]);
        return 2;
    }

    const std::string broker = argv[1];
    const std::string topic = argv[2];
    const std::vector<std::string> paths(argv + 3, argv + argc);

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    try {
        FilesMonitor monitor(paths, broker, topic);
        std::cout << "Monitoring " << paths.size() << " path(s); publishing to '" << topic
                  << "' on " << broker << ". Press Ctrl-C to stop." << std::endl;

        while (gShutdownRequested == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "Shutting down..." << std::endl;
        monitor.stop();  // the destructor then joins every monitor thread
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
