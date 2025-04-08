//
// Created by Jamster on 4/4/25.
//

int main() {
    FileMonitor monitor("/path/to/your/file.txt", "localhost:9092", "your_kafka_topic");
    monitor.monitor();
    return 0;
}
