#include <iostream>
#include <fstream>
#include <string>
#include <librdkafka/rdkafkacpp.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <stdexcept>

class FileMonitor {
public:
    FileMonitor(const std::string& filePath, const std::string& kafkaBroker, const std::string& kafkaTopic)
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
                std::cerr << "Error reading inotify events: " << strerror(errno) << std::endl;
                continue;
            }

            for (int i = 0; i < length; i += sizeof(struct inotify_event)) {
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
                            sendToKafka(line);
                        } catch (const std::exception& e) {
                            std::cerr << "Error sending message to Kafka: " << e.what() << std::endl;
                        }
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
            throw std::runtime_error("Failed to produce message: " + RdKafka::err2str(resp));
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