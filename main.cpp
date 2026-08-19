//
// Created by Jamster on 4/4/25.
//
#include "FileMonitor.h"

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace {

/// The running monitor, so the signal handler can ask it to stop.
FileMonitor* g_monitor = nullptr;

/**
 * @brief Signal handler that asks the monitor to shut down cleanly.
 *
 * FileMonitor::stop() only sets an atomic flag and writes one byte to a pipe,
 * both of which are safe to do from a signal handler.
 */
void handleSignal(int) {
    if (g_monitor != nullptr) {
        g_monitor->stop();
    }
}

} // namespace

/**
 * @brief Tails a file and forwards each appended line to a Kafka topic.
 *
 * Usage: sparky_siem [file] [broker] [topic]
 */
int main(int argc, char** argv) {
    const std::string filePath = (argc > 1) ? argv[1] : "./test.txt";
    const std::string broker   = (argc > 2) ? argv[2] : "localhost:9092";
    const std::string topic    = (argc > 3) ? argv[3] : "my-topic";

    try {
        FileMonitor monitor(filePath, broker, topic);
        g_monitor = &monitor;

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        std::cerr << "Monitoring " << filePath << " -> " << broker << "/" << topic
                  << " (Ctrl-C to stop)" << std::endl;
        monitor.monitor();

        g_monitor = nullptr;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
