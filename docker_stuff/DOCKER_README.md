# Docker Setup for SparkySIEM

This directory contains Docker configuration for building, testing, and running the SparkySIEM file change monitor.

## Files

- `Dockerfile` - Main Dockerfile for building the file monitor with all dependencies
- `Dockerfile.test` - Dockerfile for building and running unit tests
- `docker-compose.yml` - Docker Compose configuration for Kafka + monitor setup
- `README.md` - Docker documentation
- `commands` - Kafka CLI command reference
- `docker_commands_for_cli` - Additional Docker CLI commands

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

### Running Tests

```bash
cd docker_stuff
docker compose run --rm test-runner
```

This will:
1. Build the test runner container
2. Wait for Kafka to be ready
3. Run the unit tests
4. Display test results

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

## Integration Testing

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
3. Verify network connectivity

## Notes

- The Docker images are built for x86_64 architecture
- For Apple Silicon (M1/M2) Macs, use `--platform linux/amd64` flag
- The test runner installs all dependencies at runtime for maximum compatibility
