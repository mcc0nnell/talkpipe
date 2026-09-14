FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    libcurl4-openssl-dev \
    libffi-dev \
    libjansson-dev \
    libmsgpack-cxx-dev \
    libssl-dev \
    libxml2-dev \
    libzip-dev \
    ninja-build \
    nlohmann-json3-dev \
    pkg-config \
    uuid-dev \
    zip \
    && rm -rf /var/lib/apt/lists/*

COPY experiments/celix_codec_bench/install_celix_2_4_0.sh /usr/local/bin/install-celix-2.4.0
RUN chmod +x /usr/local/bin/install-celix-2.4.0 \
    && /usr/local/bin/install-celix-2.4.0 /opt/celix-2.4.0

ENV CMAKE_PREFIX_PATH=/opt/celix-2.4.0
WORKDIR /src/bench
COPY experiments/celix_codec_bench/ /src/bench/
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/celix-2.4.0 \
    && cmake --build build --parallel

WORKDIR /src/bench/build/deploy/TalkPipeCodecBenchRuntime
CMD ["./TalkPipeCodecBenchRuntime"]
