#include "FileMonitor.h"
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

FileMonitor::~FileMonitor() {
    inotify_rm_watch(inotifyFd, watchFd);
    close(inotifyFd);
    delete producer;
}

std::string FileMonitor::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));

    std::ostringstream timestamp;
    timestamp << buffer << "." << std::setfill('0') << std::setw(3) << milliseconds.count();
    return timestamp.str();
}

std::string FileMonitor::formatMessage(const std::string& filePath, const std::string& line, const std::string& kafkaTopic, const std::string& messageType) {
    std::string timestamp = getCurrentTimestamp();
    // Format the message as a JSON string
    std::string formattedMessage = "{\"timestamp\": \"" + timestamp + "\", \"filePath\": \"" + filePath + "\", \"kafkaTopic\": \"" + kafkaTopic + "\", \"message\": \"" + line + "\", \"type\": \"" + messageType + "\"}";
    return formattedMessage;
}

void FileMonitor::monitor() {
    sendToKafka(formatMessage(filePath, " ", kafkaTopic, "INIT"));
    char buffer[1024];
    
    // Open the file to check if it exists and is accessible
    // TODO: Check if the file is accessible

    sendToKafka(formatMessage(filePath, " ", kafkaTopic, "INIT - FILE OPEN"));
    // Start monitoring for file modifications
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
                    sendToKafka(formatMessage(filePath, " ", kafkaTopic, "ERROR - FILE OPEN"));
                    continue;
                }
                std::string line;
                while (std::getline(file, line)) {
                    try {
                        sendToKafka(formatMessage(filePath, line, kafkaTopic, "MODIFY"));
                    } catch (const std::exception& e) {
                        std::cerr << "Error sending message to Kafka: " << e.what() << std::endl;
                    }
                }
            }
            i += sizeof(struct inotify_event) + event->len;
        }
    }
    sendToKafka(formatMessage(filePath, " ", kafkaTopic, "CLOSE"));
    producer->flush(1000);
}

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