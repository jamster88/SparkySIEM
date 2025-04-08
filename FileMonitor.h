//
// Created by Jamster on 4/4/25.
//

#ifndef FILEMONITOR_H
#define FILEMONITOR_H

#include <iostream>
#include <fstream>
#include <string>
#include <librdkafka/rdkafkacpp.h>
#include <sys/inotify.h>
#include <unistd.h>

class FileMonitor
{
    public:
        FileMonitor(const std::string& filePath, const std::string& kafkaBroker, const std::string& kafkaTopic);
        ~FileMonitor();
        void monitor();

    private:
        sendToKafka(const std::string& message);
}

#endif //FILEMONITOR_H
