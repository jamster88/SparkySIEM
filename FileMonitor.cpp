#include "FileMonitor.h"
#include <librdkafka/rdkafkacpp.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <fstream>
#include <errno.h>
#include <chrono>

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
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));
    return std::string(buffer);
}

std::string FileMonitor::formatMessage(const std::string& filePath, const std::string& line, const std::string& kafkaTopic) {
    std::string timestamp = getCurrentTimestamp();
    // Format the message as a JSON string
    std::string formattedMessage = "{\"timestamp\": \"" + timestamp + "\", \"filePath\": \"" + filePath + "\", \"kafkaTopic\": \"" + kafkaTopic + "\", \"message\": \"" + line + "\"}";
    return formattedMessage;
}

void FileMonitor::monitor() {
    char buffer[1024];
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
                    continue;
                }
                std::string line;
                while (std::getline(file, line)) {
                    try {
                        sendToKafka(formatMessage(filePath, line, kafkaTopic));
                    } catch (const std::exception& e) {
                        std::cerr << "Error sending message to Kafka: " << e.what() << std::endl;
                    }
                }
            }
            i += sizeof(struct inotify_event) + event->len;
        }
    }
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