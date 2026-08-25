# Manual Docker Run Instructions for SparkySIEM

This guide provides step-by-step instructions for manually running the SparkySIEM containers.

## Prerequisites

- Docker installed and running
- Docker Compose installed (v2 or higher)

## Quick Start

### 1. Start Kafka

```bash
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker compose up -d kafka
```

Verify Kafka is running:
```bash
docker compose ps
docker logs -f sparky_siem_kafka
```

### 2. Build and Run Tests

```bash
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker compose run --rm test-runner
```

### 3. Run the File Monitor

```bash
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker compose run --rm file-monitor
```

## Manual Container Commands

### Build Images

```bash
# Build the file monitor image
docker build -t sparky_siem:latest -f docker_stuff/Dockerfile ..

# Build the test image
docker build -t sparky_siem_test:latest -f docker_stuff/Dockerfile.test ..
```

### Run Kafka Container

```bash
# Start Kafka in detached mode
docker run -d \
  --name sparky_siem_kafka \
  -p 9092:9092 \
  -p 9093:9093 \
  -e KAFKA_PROCESS_ROLES=broker,controller \
  -e KAFKA_LISTENERS=PLAINTEXT://0.0.0.0:9092,CONTROLLER://0.0.0.0:9093 \
  -e KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://localhost:9092 \
  -e KAFKA_CONTROLLER_LISTENER_NAMES=CONTROLLER \
  -e KAFKA_CONTROLLER_QUORUM_VOTERS=1@localhost:9093 \
  -e KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR=1 \
  -e KAFKA_GROUP_INITIAL_REBALANCE_DELAY_MS=0 \
  -e KAFKA_TRANSACTION_STATE_LOG_MIN_ISR=1 \
  -e KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR=1 \
  -v kafka_data:/var/lib/kafka/data \
  apache/kafka:3.7.0

# Wait for Kafka to be ready (about 30-45 seconds)
sleep 45

# Verify Kafka is running
docker exec sparky_siem_kafka kafka-broker-api-versions --bootstrap-server localhost:9092
```

### Run Test Container

```bash
# Create network (if not using docker-compose)
docker network create sparky_siem_network

# Run test container with build steps
docker run --rm \
  --network sparky_siem_network \
  -e KAFKA_BROKER=localhost:9092 \
  -v $(pwd)/..:/sparky_siem:ro \
  -v $(pwd)/commands:/opt/kafka/bin/commands:ro \
  ubuntu:22.04 \
  /bin/bash -c "
    apt-get update && apt-get install -y g++ make wget curl pkg-config apt-transport-https
    wget -qO- https://packages.confluent.io/deb/7.6/archive.key | gpg --dearmor -o /usr/share/keyrings/confluent-archive-keyring.gpg
    echo 'deb [signed-by=/usr/share/keyrings/confluent-archive-keyring.gpg] https://packages.confluent.io/deb/7.6 stable main' > /etc/apt/sources.list.d/confluent.list
    apt-get update && apt-get install -y librdkafka-dev libgtest-dev
    cd /usr/src/googletest && mkdir build && cd build && cmake .. && make && make install
    g++ -std=c++17 -I/usr/include /sparky_siem/test/standalone_tests.cpp /usr/lib/x86_64-linux-gnu/libgtest.a -o /sparky_siem/test/filemonitor_tests -pthread
    g++ -std=c++17 -I/usr/include /sparky_siem/test/standalone_filesmonitor_tests.cpp /usr/lib/x86_64-linux-gnu/libgtest.a -o /sparky_siem/test/filesmonitor_tests -pthread
    /sparky_siem/test/filemonitor_tests
    /sparky_siem/test/filesmonitor_tests
  "
```

### Run File Monitor Container

```bash
# Create test file
echo "Initial content" > /tmp/sparky_siem_test.txt

# Run file monitor
docker run --rm \
  --network sparky_siem_network \
  -e KAFKA_BROKER=localhost:9092 \
  -e KAFKA_TOPIC=my-topic \
  -v /tmp/sparky_siem_test.txt:/sparky_siem/test.txt \
  sparky_siem:latest

# In another terminal, send a message to Kafka
docker exec sparky_siem_kafka kafka-console-producer.sh --topic my-topic --bootstrap-server localhost:9092
```

## Running Commands in Containers

### View Kafka Logs

```bash
docker logs sparky_siem_kafka
docker logs -f sparky_siem_kafka  # Follow logs in real-time
```

### List Kafka Topics

```bash
docker exec sparky_siem_kafka kafka-topics.sh --list --bootstrap-server localhost:9092
```

### Create a New Topic

```bash
docker exec sparky_siem_kafka kafka-topics.sh --create --topic my-new-topic --bootstrap-server localhost:9092
```

### Consume Messages from a Topic

```bash
docker exec sparky_siem_kafka kafka-console-consumer.sh --topic my-topic --from-beginning --bootstrap-server localhost:9092
```

### Produce Messages to a Topic

```bash
docker exec -it sparky_siem_kafka kafka-console-producer.sh --topic my-topic --bootstrap-server localhost:9092
```

## Cleaning Up Docker Artifacts

### Note: Old ctest_broker Container

If you have an existing `ctest_broker` container from earlier testing, you can remove it with:

```bash
docker stop ctest_broker 2>/dev/null || true
docker rm ctest_broker 2>/dev/null || true
```

The SparkySIEM setup now uses `sparky_siem_kafka` as the container name instead.

### Stop and Remove All SparkySIEM Containers

```bash
# Stop and remove all containers created by docker-compose
cd /Users/jamster/Repos/SparkySIEM/docker_stuff
docker compose down

# Stop and remove all SparkySIEM containers (including manually run ones)
docker stop sparky_siem_kafka sparky_siem_test_runner sparky_siem_monitor 2>/dev/null || true
docker rm sparky_siem_kafka sparky_siem_test_runner sparky_siem_monitor 2>/dev/null || true
```

### Remove Docker Volumes

```bash
# Remove Kafka data volume
docker volume rm sparky_siem_kafka_data 2>/dev/null || true

# Remove all unused Docker volumes
docker volume prune -f
```

### Remove Docker Images

```bash
# Remove SparkySIEM images
docker rmi sparky_siem:latest sparky_siem_test:latest 2>/dev/null || true

# Remove all unused Docker images
docker image prune -f
```

### Complete Cleanup

```bash
# One-liner to remove all SparkySIEM artifacts
docker stop sparky_siem_kafka sparky_siem_test_runner sparky_siem_monitor 2>/dev/null || true
docker rm sparky_siem_kafka sparky_siem_test_runner sparky_siem_monitor 2>/dev/null || true
docker volume rm sparky_siem_kafka_data 2>/dev/null || true
docker rmi sparky_siem:latest sparky_siem_test:latest 2>/dev/null || true
```

### Verify Cleanup

```bash
# Check for remaining containers
docker ps -a | grep sparky_siem

# Check for remaining volumes
docker volume ls | grep sparky_siem

# Check for remaining images
docker images | grep sparky_siem
```

## Stopping Containers

```bash
# Stop all containers
docker compose down

# Stop and remove volumes
docker compose down -v

# Stop manually run containers
docker stop sparky_siem_kafka sparky_siem_test_runner sparky_siem_monitor
```

## Troubleshooting

### Kafka Not Starting

```bash
# Check logs
docker logs sparky_siem_kafka

# Common issue: Port already in use
# Check what's using port 9092
lsof -i :9092

# Stop the conflicting process
kill -9 <PID>
```

### Test Container Can't Reach Kafka

```bash
# Verify Kafka is running
docker ps | grep kafka

# Verify network connectivity
docker run --network sparky_siem_network --rm alpine ping -c 3 sparky_siem_kafka
```

### File Monitor Can't Write to Kafka

```bash
# Verify Kafka topic exists
docker exec sparky_siem_kafka kafka-topics.sh --list --bootstrap-server localhost:9092

# Verify file exists in container
docker run --rm -v $(pwd)/test.txt:/test.txt:ro alpine cat /test.txt
```

## Environment Variables Reference

| Variable | Description | Default |
|----------|-------------|---------|
| `KAFKA_BROKER` | Kafka broker address | `localhost:9092` |
| `KAFKA_TOPIC` | Kafka topic name | `my-topic` |
| `FILE_TO_MONITOR` | File path to monitor | `/sparky_siem/test.txt` |

## Volume Mounts

| Host Path | Container Path | Purpose |
|-----------|----------------|---------|
| `$(pwd)/..` | `/sparky_siem` | Source code (read-only) |
| `$(pwd)/commands` | `/opt/kafka/bin/commands` | Kafka command reference (read-only) |
| `/tmp/sparky_siem_test.txt` | `/sparky_siem/test.txt` | Test file (writable) |
| `kafka_data` | `/var/lib/kafka/data` | Kafka data persistence |

## Network Configuration

Containers connect to the `sparky_siem_network` network. Kafka is accessible at:

- `localhost:9092` from the host
- `sparky_siem_kafka:9092` from other containers in the network

## Quick Cleanup Command

For immediate cleanup of all SparkySIEM artifacts:

```bash
cd /Users/jamster/Repos/SparkySIEM/docker_stuff && \
docker compose down && \
docker stop sparky_siem_kafka sparky_siem_test_runner sparky_siem_monitor 2>/dev/null || true && \
docker rm sparky_siem_kafka sparky_siem_test_runner sparky_siem_monitor 2>/dev/null || true && \
docker volume rm sparky_siem_kafka_data 2>/dev/null || true && \
docker rmi sparky_siem:latest sparky_siem_test:latest 2>/dev/null || true && \
docker network rm sparky_siem_network 2>/dev/null || true && \
echo "Cleanup complete"
```
