//
// Created by Jamster on 4/4/25.
//

#include "FileMonitor.h"

#include <iostream>
#include <fstream>
#include <string>
#include <librdkafka/rdkafkacpp.h>
#include <sys/inotify.h>
#include <unistd.h>

class FileMonitor {
public:
    FileMonitor(const std::string& filePath, const std::string& kafkaBroker, const std::string& kafkaTopic)
        : filePath(filePath), kafkaBroker(kafkaBroker), kafkaTopic(kafkaTopic) {
        // Initialize Kafka producer
        std::string errstr;
        RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
        conf->set("bootstrap.servers", kafkaBroker, errstr);
        producer = RdKafka::Producer::create(conf, errstr);
        if (!producer) {
            std::cerr << "Failed to create producer: " << errstr << std::endl;
            exit(1);
        }
        delete conf;

        // Initialize inotify
        inotifyFd = inotify_init();
        if (inotifyFd < 0) {
            perror("inotify_init");
            exit(1);
        }
        watchFd = inotify_add_watch(inotifyFd, filePath.c_str(), IN_MODIFY);
        if (watchFd < 0) {
            perror("inotify_add_watch");
            exit(1);
        }
    }

    ~FileMonitor() {
        inotify_rm_watch(inotifyFd, watchFd);
        close(inotifyFd);
        delete producer;
    }

    void monitor() {
        char buffer[1024];
        while (true) {
            int length = read(inotifyFd, buffer, sizeof(buffer));
            if (length < 0) {
                perror("read");
                exit(1);
            }

            for (int i = 0; i < length; i += sizeof(struct inotify_event)) {
                struct inotify_event* event = (struct inotify_event*)&buffer[i];
                if (event->mask & IN_MODIFY) {
                    std::ifstream file(filePath);
                    std::string line;
                    while (std::getline(file, line)) {
                        sendToKafka(line);
                    }
                }
            }
        }
    }

private:
    void sendToKafka(const std::string& message) {
        RdKafka::ErrorCode resp = producer->produce(
            kafkaTopic, RdKafka::Topic::PARTITION_UA,
            RdKafka::Producer::RK_MSG_COPY,
            const_cast<char*>(message.c_str()), message.size(),
            nullptr, 0, 0, nullptr, nullptr);
        if (resp != RdKafka::ERR_NO_ERROR) {
            std::cerr << "Failed to produce message: " << RdKafka::err2str(resp) << std::endl;
        }
        producer->poll(0);
    }

    std::string filePath;
    std::string kafkaBroker;
    std::string kafkaTopic;
    RdKafka::Producer* producer;
    int inotifyFd;
    int watchFd;
};