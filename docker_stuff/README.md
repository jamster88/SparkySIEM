# SparkySIEM Docker Setup

This directory contains Docker configuration for building, testing, and running the SparkySIEM file change monitor.

## Directory Contents

- `Dockerfile` - Main Dockerfile for building the file monitor with all dependencies
- `Dockerfile.test` - Dockerfile for building and running unit tests
- `docker-compose.yml` - Docker Compose configuration for Kafka + monitor setup
- `commands` - Kafka CLI command reference

## Prerequisites

- Docker (version 20.10 or higher)
- Docker Compose (version 2.0 or higher)

## Quick Start

### Starting Kafka

```bash
cd docker_stuff
docker compose up -d
```

This starts a Kafka broker at `localhost:9092`.

### Building and Running Tests

```bash
cd docker_stuff
docker compose run --rm test-runner
```

This will:
1. Build the test runner container
2. Wait for Kafka to be ready
3. Run the unit tests
4. Start the file monitor

### Stopping

```bash
cd docker_stuff
docker compose down
```

To also remove volumes (Kafka data):

```bash
cd docker_stuff
docker compose down -v
```

## Manual Container Management

### Build the Monitor

```bash
cd docker_stuff
docker build -t sparky_siem -f Dockerfile ..
```

### Build Tests

```bash
cd docker_stuff
docker build -t sparky_siem_test -f Dockerfile.test ..
```

### Run Tests Interactively

```bash
cd docker_stuff
docker compose run --rm test-runner bash

# Inside the container
/app/test/filemonitor_tests
/app/test/filesmonitor_tests
```

## Docker Compose Services

### Kafka (`kafka`)
- Kafka broker (Apache Kafka 3.7.0)
- Port: 9092
- Health check: Enabled
- Data persists in volume `kafka_data`

### Test Runner (`test-runner`)
- Builds and runs unit tests
- Depends on Kafka
- Exits after tests complete

### File Monitor (`file-monitor`)
- Runs the file change monitor
- Sends events to Kafka
- Monitors `test.txt` in the container

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KAFKA_BROKER` | `localhost:9092` | Kafka broker address |
| `KAFKA_TOPIC` | `my-topic` | Kafka topic to send events to |
| `FILE_TO_MONITOR` | `/sparky_siem/test.txt` | File to monitor for changes |

## Testing

### Unit Tests

The test container runs two test binaries:

1. **filemonitor_tests** - Tests for FileMonitor class
   - 18 tests covering timestamp, message formatting, file operations, thread safety

2. **filesmonitor_tests** - Tests for FilesMonitor class
   - 25 tests covering path management, map operations, cleanup logic

### Integration Tests

To test with Kafka:

```bash
# Start Kafka
docker compose up -d kafka

# Build and run monitor
docker compose run --rm file-monitor

# In another terminal, watch messages
docker exec -it sparky_siem_kafka /opt/kafka/bin/kafka-console-consumer.sh \
    --topic my-topic --bootstrap-server localhost:9092

# In another terminal, modify the test file
docker exec -it sparky_siem_monitor bash
echo "Test message" >> /sparky_siem/test.txt
```

## Troubleshooting

### Kafka Not Starting

```bash
docker logs sparky_siem_kafka
```

Check for errors in the Kafka startup logs.

### Tests Not Running

```bash
docker compose run --rm test-runner bash
# Then run tests manually
/app/test/filemonitor_tests --help
```

### Connection Issues

If the monitor can't connect to Kafka:

1. Verify Kafka is running: `docker compose ps`
2. Check Kafka logs: `docker logs sparky_siem_kafka`
3. Verify network connectivity: `docker exec sparky_siem_monitor ping kafka`

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    services:
      kafka:
        image: apache/kafka:3.7.0
        ports:
          - 9092:9092
        env:
          KAFKA_PROCESS_ROLES: broker,controller
          KAFKA_LISTENERS: PLAINTEXT://0.0.0.0:9092,CONTROLLER://0.0.0.0:9093
          KAFKA_ADVERTISED_LISTENERS: PLAINTEXT://localhost:9092
          KAFKA_CONTROLLER_LISTENER_NAMES: CONTROLLER
          KAFKA_CONTROLLER_QUORUM_VOTERS: 1@localhost:9093
    steps:
      - uses: actions/checkout@v3
      
      - name: Wait for Kafka
        run: sleep 30
      
      - name: Run tests
        run: |
          ./test/filemonitor_tests
          ./test/filesmonitor_tests
```

## Volume Mounts

The test runner container mounts:
- `./commands:/opt/kafka/bin/commands:ro` - Kafka command reference
- `../test:/app/test:ro` - Test source code
- `../rand_data_gen:/app/rand_data_gen:ro` - Random data generator

## Network

All containers connect to a default network named `sparky_siem_network`.

Kafka is accessible at:
- `kafka:9092` from within the Docker network
- `localhost:9092` from the host machine
