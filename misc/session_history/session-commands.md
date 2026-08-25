# Session Commands History - SparkySIEM File Change Monitor

This file contains all terminal commands called during the SparkySIEM file change monitor verification and testing session.

## Session Summary

Date: 2026-08-25
Task: Verify file change monitor and write unit tests

---

## 1. Initial Exploration

```bash
# Find relevant files
find /Users/jamster/Repos/SparkySIEM -type f -name "*.cpp" -o -name "*.h" -o -name "*.py" -o -name "*.json" -o -name "docker-compose*" 2>/dev/null | head -100

# List root directory
ls -la /Users/jamster/Repos/SparkySIEM/

# List docker_stuff directory
ls -la /Users/jamster/Repos/SparkySIEM/docker_stuff/

# List rand_data_gen directory
ls -la /Users/jamster/Repos/SparkySIEM/rand_data_gen/
```

---

## 2. Kafka Environment Setup

```bash
# Check if Kafka is running
cd /Users/jamster/Repos/SparkySIEM/docker_stuff && docker compose ps

# Start Kafka
docker compose up -d

# Wait for Kafka and check logs
sleep 15 && docker logs ctest_broker 2>&1 | tail -20

# Create Kafka topic
docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh --create --topic my-topic --bootstrap-server localhost:9092
```

---

## 3. Random Data Generator Test

```bash
# Compile and test random data generator
cd /Users/jamster/Repos/SparkySIEM/rand_data_gen
gcc -Wall -o rand_data_gen rand_data_gen.c
./rand_data_gen 10
cat file.txt | head -5
```

---

## 4. GTest Installation

```bash
# Download gtest source
cd /tmp && curl -L https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz -o gtest.tar.gz

# Extract and build
cd /tmp/googletest-1.14.0
mkdir -p build && cd build
cmake .. && make -j4
sudo make install 2>/dev/null || make install

# Verify gtest
ls /tmp/googletest-1.14.0/build/lib/
```

---

## 5. Test Compilation

```bash
# Compile FileMonitor test
cd /tmp
g++ -std=c++17 -I/tmp/googletest-1.14.0/googletest/include -I/tmp/googletest-1.14.0/googletest -I/Users/jamster/Repos/SparkySIEM/test /Users/jamster/Repos/SparkySIEM/test/standalone_tests.cpp /tmp/googletest-1.14.0/build/lib/libgtest.a -lpthread -o standalone_tests

# Compile FilesMonitor test
g++ -std=c++17 -I/tmp/googletest-1.14.0/googletest/include -I/tmp/googletest-1.14.0/googletest -I/Users/jamster/Repos/SparkySIEM/test /Users/jamster/Repos/SparkySIEM/test/standalone_filesmonitor_tests.cpp /tmp/googletest-1.14.0/build/lib/libgtest.a -lpthread -o filesmonitor_tests
```

---

## 6. Test Execution

```bash
# Run tests
/tmp/standalone_tests
/tmp/filesmonitor_tests
```

---

## 7. Docker Setup

```bash
# Create Dockerfiles
# docker_stuff/Dockerfile
# docker_stuff/Dockerfile.test
# docker_stuff/docker-compose.yml

# Create misc directory
mkdir -p /Users/jamster/Repos/SparkySIEM/misc

# Create documentation
# misc/docker-run-instructions.md

# Verify Kafka
docker compose ps
docker exec ctest_broker /opt/kafka/bin/kafka-broker-api-versions.sh --bootstrap-server localhost:9092
docker exec ctest_broker /opt/kafka/bin/kafka-topics.sh --list --bootstrap-server localhost:9092
```

---

## 8. Cleanup

```bash
# Stop and remove ctest_broker container
docker stop ctest_broker
docker rm ctest_broker

# Verify cleanup
docker ps -a | grep -E "(ctest|sparky_siem)"
```

---

## 9. Build and Test (Docker)

```bash
# Build test Docker image
cd /Users/jamster/Repos/SparkySIEM
docker build -f docker_stuff/Dockerfile.test -t sparky_siem_test:latest .

# Build main Docker image
docker build -f docker_stuff/Dockerfile -t sparky_siem:latest .

# Run with docker-compose
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker compose up -d
docker compose run --rm test-runner
docker compose down
```

---

## 10. Complete Cleanup Commands

```bash
# Stop and remove all SparkySIEM containers
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker compose down
docker stop sparky_siem_kafka sparky_siem_test_runner sparky_siem_monitor 2>/dev/null || true
docker rm sparky_siem_kafka sparky_siem_test_runner sparky_siem_monitor 2>/dev/null || true

# Remove Docker volumes
docker volume rm sparky_siem_kafka_data 2>/dev/null || true
docker volume prune -f

# Remove Docker images
docker rmi sparky_siem:latest sparky_siem_test:latest 2>/dev/null || true
docker image prune -f

# Remove Docker network
docker network rm sparky_siem_network 2>/dev/null || true
```

---

## 11. Verification Commands

```bash
# Check container status
docker ps -a

# View logs
docker logs sparky_siem_kafka
docker logs -f sparky_siem_kafka

# List topics
docker exec sparky_siem_kafka kafka-topics.sh --list --bootstrap-server localhost:9092

# Create topic
docker exec sparky_siem_kafka kafka-topics.sh --create --topic my-new-topic --bootstrap-server localhost:9092

# Consume messages
docker exec sparky_siem_kafka kafka-console-consumer.sh --topic my-topic --from-beginning --bootstrap-server localhost:9092

# Produce messages
docker exec -it sparky_siem_kafka kafka-console-producer.sh --topic my-topic --bootstrap-server localhost:9092
```

---

## 12. File Modification Tests (Manual)

```bash
# Create test file
echo "Initial content" > /tmp/test_file.txt

# Modify file to trigger monitoring
echo "Test message at $(date)" >> /tmp/test_file.txt

# Watch for Kafka messages
docker exec -it sparky_siem_kafka kafka-console-consumer.sh --topic my-topic --from-beginning --bootstrap-server localhost:9092
```

---

## 13. Docker Compose Commands

```bash
# Start services
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker compose up -d

# View status
docker compose ps

# Follow logs
docker compose logs -f

# Stop services
docker compose down

# Stop and remove volumes
docker compose down -v

# Run single service
docker compose run --rm test-runner

# Rebuild images
docker compose build

# Run with rebuild
docker compose up --build
```

---

## 14. Environment Variables

```bash
# Set environment variables
export KAFKA_BROKER=localhost:9092
export KAFKA_TOPIC=my-topic
export FILE_TO_MONITOR=/sparky_siem/test.txt

# View environment
env | grep KAFKA
```

---

## 15. Debug Commands

```bash
# Check if port is in use
lsof -i :9092

# Check network connectivity
docker run --network sparky_siem_network --rm alpine ping -c 3 sparky_siem_kafka

# Execute shell in container
docker exec -it sparky_siem_kafka sh

# Copy files to container
docker cp /path/to/file sparky_siem_kafka:/path/in/container/
```

---

## End of Session Commands History
