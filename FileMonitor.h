/**
 * @class FileMonitor
 * @brief A class for monitoring changes to a file and sending notifications to a Kafka topic.
 *
 * The FileMonitor class provides functionality to monitor a specified file for changes
 * and send notifications about these changes to a Kafka topic. It uses inotify for
 * file monitoring and the librdkafka library for Kafka integration.
 *
 * @details
 * - The class is initialized with the file path to monitor, the Kafka broker address,
 *   and the Kafka topic to which messages will be sent.
 * - It continuously monitors the file for changes and sends formatted messages to
 *   the Kafka topic when changes are detected.
 * - The class also provides utility functions for timestamp generation, message formatting,
 *   and Kafka message sending.
 *
 * @note
 * - This class requires the librdkafka library for Kafka integration.
 * - The inotify API is used for file monitoring, which is specific to Linux systems.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 * @warning This class assumes that the file path and Kafka topic are valid and accessible.
 *          Ensure proper error handling for file access and Kafka message sending.
 * @todo Implement a mechanism to gracefully terminate the monitoring loop.
 * @todo Add error handling for Kafka message sending failures.
 * @todo Consider adding support for monitoring multiple files or directories.
 * @todo Implement a configuration file for Kafka settings and file paths.
 */

#ifndef FILEMONITOR_H
#define FILEMONITOR_H

#include <string>
#include <librdkafka/rdkafkacpp.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <errno.h>
#include <chrono>  


/**
 * @brief Monitors a file for changes and sends notifications to a Kafka topic.
 *
 * The FileMonitor class uses inotify to monitor a specified file for changes
 * and sends formatted messages to a Kafka topic using the provided Kafka broker.
 */
class FileMonitor {
public:
    /**
     * @brief Constructs a FileMonitor object.
     * @param filePath The path of the file to monitor.
     * @param kafkaBroker The address of the Kafka broker.
     * @param kafkaTopic The Kafka topic to which messages will be sent.
     */
    FileMonitor(const std::string& filePath, const std::string& kafkaBroker, const std::string& kafkaTopic);

    /**
     * @brief Destroys the FileMonitor object and releases resources.
     */
    ~FileMonitor();

    /**
     * @brief Starts monitoring the specified file for changes.
     *
     * This function blocks and continuously monitors the file for changes,
     * sending notifications to the Kafka topic when changes are detected.
     */
    void monitor();

private:
    /**
     * @brief Retrieves the current timestamp in a formatted string.
     * @return A string representing the current timestamp.
     */
    std::string getCurrentTimestamp();

    /**
     * @brief Formats a message to be sent to the Kafka topic.
     * @param filePath The path of the file being monitored.
     * @param line The content of the line that triggered the event.
     * @param kafkaTopic The Kafka topic to which the message will be sent.
     * @param messageType The type of message (e.g., "MODIFY", "DELETE").
     * @return A formatted string containing the message.
     */
    std::string formatMessage(const std::string& filePath, const std::string& line, const std::string& kafkaTopic, const std::string& messageType);

    /**
     * @brief Sends a message to the Kafka topic.
     * @param message The message to be sent.
     */
    void sendToKafka(const std::string& message);

    // Member variables
    std::string filePath; ///< The path of the file being monitored.
    std::string kafkaBroker; ///< The address of the Kafka broker.
    std::string kafkaTopic; ///< The Kafka topic to which messages are sent.
    RdKafka::Producer* producer; ///< Pointer to the Kafka producer instance.
    int inotifyFd; ///< File descriptor for the inotify instance.
    int watchFd; ///< File descriptor for the inotify watch.
};

#endif