# Dockerfile for SparkySIEM File Change Monitor
# This builds the monitor with librdkafka

FROM ubuntu:22.04

# Install build dependencies
RUN apt-get update && apt-get install -y \
    g++ \
    make \
    cmake \
    git \
    wget \
    curl \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install librdkafka from Confluent's repository
RUN apt-get install -y apt-transport-https && \
    wget -qO- https://packages.confluent.io/deb/7.6/archive.key | gpg --dearmor -o /usr/share/keyrings/confluent-archive-keyring.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/confluent-archive-keyring.gpg] https://packages.confluent.io/deb/7.6 stable main" > /etc/apt/sources.list.d/confluent.list && \
    apt-get update && \
    apt-get install -y librdkafka-dev && \
    rm -rf /var/lib/apt/lists/*

# Copy source code
WORKDIR /sparky_siem
COPY *.cpp *.h /sparky_siem/
COPY rand_data_gen/rand_data_gen.c /sparky_siem/rand_data_gen/
COPY test.txt /sparky_siem/

# Build main executable
RUN g++ -std=c++17 \
    -I/usr/include \
    /sparky_siem/main.cpp \
    /sparky_siem/FileMonitor.cpp \
    -lrdkafka \
    -o /sparky_siem/sparky_siem

# Default to running the monitor
CMD ["/sparky_siem/sparky_siem"]
