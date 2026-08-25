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

int main(int argc, char** argv) {
    std::string filePath = "/Users/jamster/Repos/SparkySIEM/test.txt";
    std::string kafkaBroker = "localhost:9092";
    std::string kafkaTopic = "my-topic";

    // Parse command line arguments if provided
    if (argc > 1) filePath = argv[1];
    if (argc > 2) kafkaBroker = argv[2];
    if (argc > 3) kafkaTopic = argv[3];

    try {
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
