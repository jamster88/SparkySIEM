/**
 * @file FileMonitor.cpp
 * @brief Implementation of the FileMonitor class for monitoring file modifications and sending events to a Kafka topic.
 * 
 * This file contains the implementation of the FileMonitor class, which uses inotify to monitor
 * a specified file for modifications and sends the detected changes as messages to a Kafka topic.
 * The class provides functionality for initializing Kafka producers, setting up inotify watches,
 * formatting messages as JSON, and continuously monitoring the file for changes.
 * 
 * @details
 * - The constructor initializes the Kafka producer and inotify watch.
 * - The destructor cleans up resources such as the inotify watch and Kafka producer.
 * - The `getCurrentTimestamp` method generates a timestamp string in the format "YYYY-MM-DD HH:MM:SS.mmm".
 * - The `formatMessage` method formats metadata and content into a JSON string.
 * - The `monitor` method runs an infinite loop to detect file modifications and send updates to Kafka.
 * - The `sendToKafka` method sends messages to the Kafka topic and handles errors during production.
 * 
 * @note
 * - Ensure that the Kafka broker and topic are properly configured before using this class.
 * - The `monitor` method runs indefinitely and does not provide a built-in mechanism for graceful termination.
 * 
 * @warning
 * - The `monitor` method assumes the file exists and is accessible. Proper error handling is implemented for file access issues.
 * - Ensure that the Kafka producer is properly initialized to avoid runtime exceptions.
 * 
 * @todo
 * - Add a mechanism to gracefully terminate the infinite loop in the `monitor` method.
 * - Implement a check to ensure the file is accessible before starting monitoring.
 * 
 * Dependencies:
 * - C++17 filesystem library for file and directory operations.
 * 
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 */

#include "FileMonitor.h"
#include "FileFormat.h"  // Pure formatting utilities (timestamp, JSON escaping)
#include <librdkafka/rdkafkacpp.h> // Used for Kafka producer
#include <sys/inotify.h>           // Used for inotify functions
#include <unistd.h>                // Used for close()
#include <stdexcept>               // Used for std::runtime_error
#include <cstring>                 // Used for strerror()
#include <sstream>                 // Used for std::ostringstream
#include <iomanip>                 // Used for std::setfill and std::setw
#include <iostream>                // Used for std::cerr
#include <fstream>                 // Used for std::ifstream
#include <errno.h>                 // Used for errno
#include <chrono>                  // Used for timestamp generation

/**
 * @brief Constructs a FileMonitor object to monitor a file for modifications and send events to a Kafka topic.
 * 
 * @param filePath The path of the file to monitor for modifications.
 * @param kafkaBroker The Kafka broker address to connect to.
 * @param kafkaTopic The Kafka topic to which file modification events will be sent.
 * 
 * @throws std::runtime_error If Kafka producer initialization fails or inotify setup fails.
 * 
 * This constructor initializes the Kafka producer with the specified broker and topic,
 * and sets up inotify to monitor the specified file for modifications. If any of these
 * steps fail, an exception is thrown with an appropriate error message.
 */
FileMonitor::FileMonitor(const std::string& filePath, const std::string& kafkaBroker, const std::string& kafkaTopic)
    : filePath(filePath), kafkaBroker(kafkaBroker), kafkaTopic(kafkaTopic) {
    // Initialize Kafka producer
    std::string errstr;
    RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    if (conf->set("bootstrap.servers", kafkaBroker, errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set Kafka broker: " + errstr);
    }
    producer = RdKafka::Producer::create(conf, errstr);
    if (!producer) {
        throw std::runtime_error("Failed to create Kafka producer: " + errstr);
    }
    delete conf;

    // Initialize inotify
    inotifyFd = inotify_init();
    if (inotifyFd < 0) {
        throw std::runtime_error("Failed to initialize inotify: " + std::string(strerror(errno)));
    }
    watchFd = inotify_add_watch(inotifyFd, filePath.c_str(), IN_MODIFY);
    if (watchFd < 0) {
        throw std::runtime_error("Failed to add inotify watch: " + std::string(strerror(errno)));
    }
}

/**
 * @brief Destructor for the FileMonitor class.
 *
 * This destructor is responsible for cleaning up resources used by the
 * FileMonitor instance. It removes the inotify watch, closes the inotify
 * file descriptor, and deletes the producer object to prevent memory leaks.
 */
FileMonitor::~FileMonitor() {
    inotify_rm_watch(inotifyFd, watchFd);
    close(inotifyFd);
    delete producer;
}

/**
 * @brief Formats a message as JSON and sends it to Kafka.
 */
void FileMonitor::formatAndSend(const std::string& line, const std::string& messageType) {
    std::string formatted = formatMessage(filePath, line, kafkaTopic, messageType);
    sendToKafka(formatted);
}

/**
 * @brief Monitors a file for modifications and sends updates to a Kafka topic.
 *
 * This function uses inotify to monitor the specified file for changes. When a modification
 * is detected, it reads the updated content of the file and sends each line as a message
 * to a Kafka topic. The function runs indefinitely in a loop until terminated.
 *
 * @details
 * - Sends an "INIT" message to Kafka when monitoring starts.
 * - Sends an "INIT - FILE OPEN" message to Kafka after verifying the file is accessible.
 * - Monitors the file for `IN_MODIFY` events using inotify.
 * - Reads the modified file line by line and sends each line to Kafka with a "MODIFY" tag.
 * - Handles errors such as file access issues or Kafka message sending failures.
 * - Sends a "CLOSE" message to Kafka before exiting the function.
 *
 * @note This function assumes that the file path and Kafka topic are properly initialized.
 *       It also assumes that the Kafka producer is set up and accessible.
 *
 * @warning The function runs an infinite loop and does not provide a mechanism for graceful
 *          termination. Ensure proper handling to stop the loop when needed.
 *
 * @todo Add a check to ensure the file is accessible before starting monitoring.
 * @todo Implement a mechanism to gracefully terminate the infinite loop.
 */
void FileMonitor::monitor() {
    // Send initialization messages
    formatAndSend(" ", "INIT");
    char buffer[1024];

    // Check if the file is accessible before starting monitoring
    std::ifstream testFile(filePath);
    if (!testFile.is_open()) {
        formatAndSend(" ", "ERROR - FILE OPEN");
    } else {
        formatAndSend(" ", "INIT - FILE OPEN");
        testFile.close();
    }

    // Start monitoring for file modifications (infinite loop, blocks on read)
    while (true) {
        int length = read(inotifyFd, buffer, sizeof(buffer));
        if (length < 0) {
            std::cerr << "Error reading inotify events: " << strerror(errno) << std::endl;
            continue;
        }

        for (int i = 0; i < length;) {
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
            if (event->mask & IN_MODIFY) {
                std::ifstream file(filePath);
                if (!file.is_open()) {
                    std::cerr << "Failed to open file: " << filePath << std::endl;
                    formatAndSend(" ", "ERROR - FILE OPEN");
                    continue;
                }
                std::string line;
                while (std::getline(file, line)) {
                    try {
                        formatAndSend(line, "MODIFY");
                    } catch (const std::exception& e) {
                        std::cerr << "Error sending message to Kafka: " << e.what() << std::endl;
                    }
                }
            }
            i += sizeof(struct inotify_event) + event->len;
        }
    }
}

/**
 * @brief Sends a message to a Kafka topic using the configured Kafka producer.
 *
 * This method produces a message to the specified Kafka topic and handles any
 * errors that may occur during the production process. If the message fails
 * to be produced, an exception is thrown with the corresponding error message.
 *
 * @param message The message to be sent to the Kafka topic.
 *
 * @throws std::runtime_error If the message fails to be produced, an exception
 *         is thrown with the error description.
 */
void FileMonitor::sendToKafka(const std::string& message) {
    RdKafka::ErrorCode resp = producer->produce(
        kafkaTopic, RdKafka::Topic::PARTITION_UA,
        RdKafka::Producer::RK_MSG_COPY,
        const_cast<char*>(message.c_str()), message.size(),
        nullptr, 0, 0, nullptr, nullptr);
    if (resp != RdKafka::ERR_NO_ERROR) {
        throw std::runtime_error("Failed to produce message: " + RdKafka::err2str(resp));
    }
    producer->poll(0);
}