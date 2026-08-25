/**
 * @file main.cpp
 * @brief Main entry point for the SparkySIEM file monitor application.
 *
 * This application monitors a specified file for changes and sends events
 * to a Kafka topic. It uses the FileMonitor class for file monitoring and
 * Kafka integration.
 *
 * Usage:
 *   ./sparkysiem [file_path] [kafka_broker] [kafka_topic]
 *
 * Arguments:
 *   file_path     Path to the file to monitor (default: /Users/jamster/Repos/SparkySIEM/test.txt)
 *   kafka_broker  Kafka broker address (default: localhost:9092)
 *   kafka_topic   Kafka topic name (default: my-topic)
 *
 * Example:
 *   ./sparkysiem /var/log/syslog localhost:9092 system-logs
 *
 * @note Press Ctrl+C to gracefully terminate the application (if implemented).
 * @note This application runs indefinitely until manually stopped.
 */

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

/**
 * @brief Main entry point for the SparkySIEM application.
 *
 * This function initializes the file monitor with the provided arguments
 * and starts monitoring for file changes. It handles command-line arguments
 * with defaults for optional parameters.
 *
 * @param argc The number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return int Exit code (0 for success, 1 for error).
 *
 * @throws std::exception Catches and logs any errors during initialization or monitoring.
 */
int main(int argc, char** argv) {
    // Set default values
    std::string filePath = "/Users/jamster/Repos/SparkySIEM/test.txt";
    std::string kafkaBroker = "localhost:9092";
    std::string kafkaTopic = "my-topic";

    // Parse command line arguments if provided
    if (argc > 1) filePath = argv[1];
    if (argc > 2) kafkaBroker = argv[2];
    if (argc > 3) kafkaTopic = argv[3];

    try {
        // Create and start the file monitor
        FileMonitor monitor(filePath, kafkaBroker, kafkaTopic);
        std::cout << "Starting file monitor for: " << filePath << std::endl;
        std::cout << "Kafka broker: " << kafkaBroker << std::endl;
        std::cout << "Kafka topic: " << kafkaTopic << std::endl;
        std::cout << "Press Ctrl+C to stop..." << std::endl;
        monitor.monitor();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
