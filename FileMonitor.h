#ifndef FILEMONITOR_H
#define FILEMONITOR_H

#include <string>
#include <librdkafka/rdkafkacpp.h>

class FileMonitor {
public:
    FileMonitor(const std::string& filePath, const std::string& kafkaBroker, const std::string& kafkaTopic);
    ~FileMonitor();
    void monitor();

private:
    std::string getCurrentTimestamp();
    std::string formatMessage(const std::string& filePath, const std::string& line, const std::string& kafkaTopic, const std::string& messageType);
    void sendToKafka(const std::string& message);

    std::string filePath;
    std::string kafkaBroker;
    std::string kafkaTopic;
    RdKafka::Producer* producer;
    int inotifyFd;
    int watchFd;
};

#endif