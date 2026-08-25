# SparkySIEM — Manual Docker Run Instructions

All containers below run a Debian-based Linux environment with the libraries SparkySIEM needs (inotify, librdkafka). macOS / Windows users need a container runtime (Docker Desktop, OrbStack, colima) since inotify and Kafka are Linux-only.

---

## 1. Start the Kafka Broker

```bash
# Pull the image first
docker pull apache/kafka:latest

# Run the broker on port 9092
docker run -d \
  --name sparkysiem_kafka \
  --publish 9092:9092 \
  --env KAFKA_BROKER_ID=1 \
  --env KAFKA_LISTENER_SECURITY_PROTOCOL_MAP=PLAINTEXT:PLAINTEXT,PLAINTEXT_HOST:PLAINTEXT,CONTROLLER:PLAINTEXT \
  --env KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://sparkysiem_kafka:29092,PLAINTEXT_HOST://localhost:9092 \
  --env KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR=1 \
  --env KAFKA_GROUP_INITIAL_REBALANCE_DELAY_MS=0 \
  --env KAFKA_TRANSACTION_STATE_LOG_MIN_ISR=1 \
  --env KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR=1 \
  --env KAFKA_PROCESS_ROLES=broker,controller \
  --env KAFKA_NODE_ID=1 \
  --env KAFKA_CONTROLLER_QUORUM_VOTERS=1@sparkysiem_kafka:29093 \
  --env KAFKA_LISTENERS=PLAINTEXT://sparkysiem_kafka:29092,CONTROLLER://sparkysiem_kafka:29093,PLAINTEXT_HOST://0.0.0.0:9092 \
  --env KAFKA_INTER_BROKER_LISTENER_NAME=PLAINTEXT \
  --env KAFKA_CONTROLLER_LISTENER_NAMES=CONTROLLER \
  --env KAFKA_LOG_DIRS=/tmp/kraft-combined-logs \
  --env CLUSTER_ID=SparkySIEMKafkaCluster01 \
  apache/kafka:latest

# Wait for it to be ready (about 10-15 seconds)
docker logs -f sparkysiem_kafka    # Ctrl-C when you see "started"
```

**Verify:**
```bash
docker exec sparkysiem_kafka /opt/kafka/bin/kafka-topics.sh \
  --bootstrap-server localhost:9092 --list
```

---

## 2. Create the SparkySIEM Topic

```bash
docker exec sparkysiem_kafka sh -c \
  '/opt/kafka/bin/kafka-topics.sh --create --topic sparky-changes --bootstrap-server localhost:9092'
```

---

## 3. Build the Docker Image (Builder + Test Tools)

The builder image installs gcc, cmake, librdkafka-dev, and all compile-time dependencies. It also carries GoogleTest so you can build + run tests from one container.

```bash
cd docker_stuff

docker build -t sparkysiem-builder -f Dockerfile.build ..
docker build -t sparkysiem-tester  -f Dockerfile.test  ..
```

*(Both images take a few minutes to pull dependencies and compile GoogleTest on first run.)*

---

## 4. Compile SparkySIEM Inside the Container

```bash
# Start an interactive shell in the builder image
docker run -it --rm \
  --name sparkysiem_builder \
  --network host \
  -v $(pwd)/../:/opt/sparkysiem/src \
  sparkysiem-builder bash
```

Inside the container:

```bash
cd /opt/sparkysiem/src

# Compile file_monitor
mkdir -p build
g++ -std=c++17 -I/usr/include FileFormat.cpp FileMonitor.cpp main.cpp \
  -o build/file_monitor \
  -L/usr/lib/aarch64-linux-gnu -lrdkafka++ -lrdkafka \
  -lpthread -lz -lssl -lcrypto

# Compile files_monitor (multi-file monitor)
g++ -std=c++17 -I/usr/include FileFormat.cpp FilesMonitor.cpp main.cpp \
  -o build/files_monitor \
  -L/usr/lib/aarch64-linux-gnu -lrdkafka++ -lrdkafka \
  -lpthread -lz -lssl -lcrypto

exit
```

---

## 5. Run the Monitor (End-to-End)

```bash
docker run --rm \
  --name sparkysiem_monitor \
  --network host \
  -v $(pwd)/../:/opt/sparkysiem/src \
  sparkysiem-builder \
  /opt/sparkysiem/src/build/file_monitor
```

Then in a **separate terminal**, modify the monitored file (`/tmp/test.txt` by default):

```bash
echo "hello world" > /tmp/test.txt
echo "another change" >> /tmp/test.txt
```

In another terminal, consume from Kafka to verify:

```bash
docker exec sparkysiem_kafka /opt/kafka/bin/kafka-console-consumer.sh \
  --topic sparky-changes \
  --from-beginning \
  --bootstrap-server localhost:9092
```

Expected output (JSON):
```json
{"timestamp": "2026-08-25 03:44:50.396", "filePath": "/tmp/test.txt", "kafkaTopic": "sparky-changes", "message": "line1", "type": "MODIFY"}
```

---

## 6. Run Unit Tests Inside the Container

All three test suites compile and run inside the tester container (no host-side deps needed).

```bash
# Start an interactive shell in the tester image
docker run -it --rm \
  --name sparkysiem_tester \
  --network host \
  -v $(pwd)/../:/opt/sparkysiem/src \
  sparkysiem-tester bash
```

Inside the container:

```bash
cd /opt/sparkysiem/src/tests
mkdir -p build

# Compile all three suites
g++ -std=c++17 -I.. test_file_format.cpp ../FileFormat.cpp -o build/test_file_format   -lgtest -lgtest_main -lpthread
g++ -std=c++17 -I.. test_files_monitor_logic.cpp  ../FileFormat.cpp -o build/test_files_monitor_logic -lgtest -lpthread
g++ -std=c++17 -I.. test_runner.cpp ../FileFormat.cpp -o build/test_runner

# Run them
./build/test_file_format   # 17 tests
./build/test_files_monitor_logic   # 17 tests
./build/test_runner              # 14 tests (no external deps)
```

Expected: **48 / 48 passed**.

---

## 7. Start Everything at Once (docker run equivalents of the compose file)

```bash
# Step 1: Create a Docker network so containers can find each other by name
docker network create sparkysiem-net

# Step 2: Kafka broker
docker run -d \
  --name sparkysiem_kafka \
  --network sparkysiem-net \
  --publish 9092:9092 \
  --env KAFKA_BROKER_ID=1 \
  --env KAFKA_LISTENER_SECURITY_PROTOCOL_MAP=PLAINTEXT:PLAINTEXT,PLAINTEXT_HOST:PLAINTEXT,CONTROLLER:PLAINTEXT \
  --env KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://sparkysiem_kafka:29092,PLAINTEXT_HOST://localhost:9092 \
  --env KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR=1 \
  --env KAFKA_GROUP_INITIAL_REBALANCE_DELAY_MS=0 \
  --env KAFKA_TRANSACTION_STATE_LOG_MIN_ISR=1 \
  --env KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR=1 \
  --env KAFKA_PROCESS_ROLES=broker,controller \
  --env KAFKA_NODE_ID=1 \
  --env KAFKA_CONTROLLER_QUORUM_VOTERS=1@sparkysiem_kafka:29093 \
  --env KAFKA_LISTENERS=PLAINTEXT://sparkysiem_kafka:29092,CONTROLLER://sparkysiem_kafka:29093,PLAINTEXT_HOST://0.0.0.0:9092 \
  --env KAFKA_INTER_BROKER_LISTENER_NAME=PLAINTEXT \
  --env KAFKA_CONTROLLER_LISTENER_NAMES=CONTROLLER \
  --env KAFKA_LOG_DIRS=/tmp/kraft-combined-logs \
  --env CLUSTER_ID=SparkySIEMKafkaCluster01 \
  apache/kafka:latest

sleep 15   # wait for Kafka to be ready

# Step 3: Builder (keeps running via tail -f)
docker run -it --rm \
  --name sparkysiem_builder \
  --network sparkysiem-net \
  -v $(pwd)/../:/opt/sparkysiem/src \
  sparkysiem-builder tail -f /dev/null

# Step 4: Tester (keeps running via tail -f)
docker run -it --rm \
  --name sparkysiem_tester \
  --network sparkysiem-net \
  -v $(pwd)/../:/opt/sparkysiem/src \
  sparkysiem-tester tail -f /dev/null
```

Once the builder and tester containers are running, use `docker exec` to interact with them:

```bash
# Compile inside the builder
docker exec sparkysiem_builder sh -c 'cd /opt/sparkysiem/src && g++ -std=c++17 -I/usr/include FileFormat.cpp FileMonitor.cpp main.cpp -o build/file_monitor -L/usr/lib/aarch64-linux-gnu -lrdkafka++ -lrdkafka -lpthread -lz -lssl -lcrypto'

# Run tests inside the tester
docker exec sparkysiem_tester sh -c 'cd /opt/sparkysiem/src/tests && g++ -std=c++17 -I.. test_runner.cpp ../FileFormat.cpp -o build/test_runner && ./build/test_runner'

# Run the monitor (points Kafka at the broker via the Docker network)
docker run --rm \
  --network sparkysiem-net \
  -v $(pwd)/../:/opt/sparkysiem/src \
  sparkysiem-builder /opt/sparkysiem/src/build/file_monitor
```

---

## 8. Cleanup — Stop and Remove All Artifacts

### Stop all running containers

```bash
docker stop sparkysiem_kafka sparkysiem_builder sparkysiem_tester
```

### Remove the containers

```bash
docker rm sparkysiem_kafka sparkysiem_builder sparkysiem_tester
```

### Remove the shared Docker network (if nothing else uses it)

```bash
docker network rm sparkysiem-net 2>/dev/null
```

### Remove the SparkySIEM images and the Kafka image

```bash
docker rmi sparkysiem-builder sparkysiem-tester apache/kafka:latest 2>/dev/null
```

### Prune dangling build layers (reclaims most disk space)

```bash
docker image prune -f
```

### Remove everything including stopped containers and unused volumes

> **Warning:** this affects *all* Docker projects on this machine, not just SparkySIEM.

```bash
docker system prune -af --volumes
```

### One-shot cleanup (compose)

If you started everything with the compose file:

```bash
docker compose -f docker-compose.yaml down --rmi all --volumes
```

---

## 9. Notes

| Item | Detail |
|---|---|
| Broker address for code | `sparkysiem_kafka:29092` (Docker network) or `localhost:9092` (host-mapped port) |
| Default monitored file | `/tmp/test.txt` (change via source edit) |
| Default Kafka topic | `sparky-changes` |
| Platform requirement | inotify is Linux-only — run SparkySIEM inside a Docker/Linux container, not natively on macOS/Windows |
| Port 9092 | Mapped from container port 29092; accessible from host as `localhost:9092` |
