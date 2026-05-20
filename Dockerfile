FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config \
    libboost-system-dev libboost-thread-dev \
    libprotobuf-dev protobuf-compiler \
    libspdlog-dev libfmt-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# librdkafka - 锁定版本
RUN git clone --depth 1 --branch v2.3.0 https://github.com/confluentinc/librdkafka.git /tmp/librdkafka \
    && cd /tmp/librdkafka \
    && ./configure --prefix=/usr/local \
    && make -j$(nproc) && make install \
    && rm -rf /tmp/librdkafka

# hiredis + redis++
RUN git clone --depth 1 --branch v1.14.0 https://github.com/redis/hiredis.git /tmp/hiredis \
    && cd /tmp/hiredis && make -j$(nproc) && make install \
    && rm -rf /tmp/hiredis
RUN git clone --depth 1 --branch v1.3.12 https://github.com/sewenew/redis-plus-plus.git /tmp/redis-plus-plus \
    && cd /tmp/redis-plus-plus \
    && mkdir build && cd build \
    && cmake .. -DREDIS_PLUS_PLUS_CXX_STANDARD=17 -DREDIS_PLUS_PLUS_BUILD_TEST=OFF \
    && make -j$(nproc) && make install \
    && rm -rf /tmp/redis-plus-plus

# clickhouse-cpp - 锁定版本
RUN git clone --depth 1 --branch v2.5.1 https://github.com/ClickHouse/clickhouse-cpp.git /tmp/clickhouse-cpp \
    && cd /tmp/clickhouse-cpp \
    && mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc) && make install \
    && rm -rf /tmp/clickhouse-cpp

COPY . /src
WORKDIR /src/build
RUN cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc)

FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libboost-system1.74.0 libboost-thread1.74.0 \
    libprotobuf23 libspdlog1.9 libfmt8 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib /usr/local/lib
COPY --from=builder /src/build/server /app/server
RUN ldconfig

WORKDIR /app
EXPOSE 8080 9090

# 以非 root 用户运行，降低容器逃逸风险
RUN addgroup -S appgroup && adduser -S appuser -G appgroup
USER appuser

CMD ["./server"]
