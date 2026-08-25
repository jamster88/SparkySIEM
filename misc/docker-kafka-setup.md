# Docker Kafka Setup for End-to-End Testing

## Prerequisites
Docker Desktop installed and running

## Start Kafka Broker
```bash
cd docker_stuff
docker compose up -d
docker logs ctest_broker    # verify broker is ready
```

## Create Topic
```bash
docker exec -it -w /opt/kafka/bin ctest_broker sh
./kafka-topics.sh --create --topic my-topic --bootstrap-server ctest_broker:29092
exit
```

## Run Producer (while consumer listens)
```bash
# In one terminal, start consumer:
docker exec -it -w /opt/kafka/bin ctest_broker sh
./kafka-console-consumer.sh --topic my-topic --from-beginning --bootstrap-server ctest_broker:29092

# In another terminal, modify monitored file:
echo "hello world" > test.txt
```

## Stop Kafka
```bash
cd docker_stuff
docker compose down -v
```

## Notes
- Broker exposed on port 9092 (mapped from container port 29092)
- Default bootstrap server: `localhost:9092`
- FileMonitor and FilesMonitor both default to this address
- The rand_data_gen program can be used to generate test file changes
