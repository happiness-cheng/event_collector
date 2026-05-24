FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config \
    libboost-system-dev libboost-thread-dev \
    libprotobuf-dev protobuf-compiler \
    libspdlog-dev libfmt-dev \
    libssl-dev ca-certificates \
    liblz4-dev libzstd-dev \
    && apt-get clean

# librdkafka
RUN git clone --depth 1 --branch v2.3.0 https://github.com/confluentinc/librdkafka.git /tmp/lk \
    && cd /tmp/lk && ./configure --prefix=/usr/local && make -j2 && make install

# hiredis
RUN git clone --depth 1 --branch v1.3.0 https://github.com/redis/hiredis.git /tmp/hd \
    && cd /tmp/hd && make -j2 && make install

# redis-plus-plus
RUN git clone --depth 1 --branch 1.3.12 https://github.com/sewenew/redis-plus-plus.git /tmp/rp \
    && cd /tmp/rp && mkdir b && cd b \
    && cmake .. -DREDIS_PLUS_PLUS_CXX_STANDARD=17 -DREDIS_PLUS_PLUS_BUILD_TEST=OFF \
    && make -j2 && make install

# clickhouse-cpp
RUN git clone --depth 1 --branch v2.5.1 https://github.com/ClickHouse/clickhouse-cpp.git /tmp/ch \
    && cd /tmp/ch && mkdir b && cd b \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j2 && make install \
    && cp /tmp/ch/contrib/cityhash/cityhash/city.h /usr/local/include/ \
    && cp /tmp/ch/b/contrib/cityhash/cityhash/libcityhash.a /usr/local/lib/ \
    && cp -r /tmp/ch/contrib/absl/absl /usr/local/include/ \
    && cp /tmp/ch/b/contrib/absl/absl/libabsl_int128.a /usr/local/lib/

COPY . /src
RUN protoc --cpp_out=/src/include --proto_path=/src/proto /src/proto/event.proto \
    && cp /src/include/event.pb.cc /src/src/proto/event.pb.cc
WORKDIR /src/build
RUN cmake .. -DCMAKE_BUILD_TYPE=Release && make -j2

FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libboost-system1.74.0 libboost-thread1.74.0 \
    libprotobuf23 libspdlog1 libfmt8 \
    libssl3 ca-certificates \
    && apt-get clean

COPY --from=builder /usr/local/lib /usr/local/lib
COPY --from=builder /src/build/server /app/server
RUN ldconfig

WORKDIR /app
EXPOSE 8080 9090

RUN addgroup -S appgroup && adduser -S appuser -G appgroup
USER appuser

CMD ["./server"]
