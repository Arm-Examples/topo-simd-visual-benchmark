# Multi-stage Dockerfile for building scalar, NEON, and SVE variants

# Base builder stage with common dependencies
FROM --platform=linux/arm64/v8 ubuntu:24.04 AS builder-base

RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    wget \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Download and install header-only dependencies
RUN git clone --depth 1 --branch v0.14.3 https://github.com/yhirose/cpp-httplib.git && \
    mkdir -p /usr/local/include && \
    cp cpp-httplib/httplib.h /usr/local/include/ && \
    rm -rf cpp-httplib

# Download nlohmann/json (header-only)
RUN git clone --depth 1 --branch v3.11.2 https://github.com/nlohmann/json.git && \
    mkdir -p /usr/local/include/nlohmann && \
    cp -r json/include/nlohmann/. /usr/local/include/nlohmann/ && \
    rm -rf json

# Copy source files
COPY image_processor.cpp server.cpp ./

# Scalar variant - no SIMD optimizations
FROM builder-base AS builder-scalar

ENV CXXFLAGS="-O2 -march=armv8-a+nosimd -fno-tree-vectorize -fno-tree-slp-vectorize"
ENV CC=aarch64-linux-gnu-gcc \
    CXX=aarch64-linux-gnu-g++ \
    STRIP=aarch64-linux-gnu-strip

RUN ${CXX} -o processor server.cpp image_processor.cpp \
    ${CXXFLAGS} \
    -std=c++17 \
    -pthread \
    -I/usr/local/include \
    -static-libstdc++ \
    -static-libgcc \
    && ${STRIP} processor

# NEON variant - NEON optimizations enabled
FROM builder-base AS builder-neon

ENV CXXFLAGS="-O3 -march=armv8-a -mtune=cortex-a72 -ftree-vectorize"
ENV CC=aarch64-linux-gnu-gcc \
    CXX=aarch64-linux-gnu-g++ \
    STRIP=aarch64-linux-gnu-strip

RUN ${CXX} -o processor server.cpp image_processor.cpp \
    ${CXXFLAGS} \
    -std=c++17 \
    -pthread \
    -I/usr/local/include \
    -static-libstdc++ \
    -static-libgcc \
    && ${STRIP} processor

# SVE variant - SVE2 optimizations enabled
FROM builder-base AS builder-sve

ENV CXXFLAGS="-O3 -march=armv9-a+sve -mtune=neoverse-v2 -ftree-vectorize"
ENV CC=aarch64-linux-gnu-gcc \
    CXX=aarch64-linux-gnu-g++ \
    STRIP=aarch64-linux-gnu-strip

RUN ${CXX} -o processor server.cpp image_processor.cpp \
    ${CXXFLAGS} \
    -std=c++17 \
    -pthread \
    -I/usr/local/include \
    -static-libstdc++ \
    -static-libgcc \
    && ${STRIP} processor

# Final scalar runtime
FROM --platform=linux/arm64/v8 ubuntu:24.04 AS app-scalar

ENV BUILD_VARIANT=scalar \
    BUILD_MARCH=armv8-a+nosimd \
    BUILD_OPTIMIZATION=scalar-only \
    SERVICE_PORT=8000

WORKDIR /app

COPY --from=builder-scalar /build/processor .

EXPOSE 8000

CMD ["./processor"]

# Final NEON runtime
FROM --platform=linux/arm64/v8 ubuntu:24.04 AS app-neon

ENV BUILD_VARIANT=neon \
    BUILD_MARCH=armv8-a \
    BUILD_OPTIMIZATION=neon-vectorized \
    SERVICE_PORT=8000

WORKDIR /app

COPY --from=builder-neon /build/processor .

EXPOSE 8000

CMD ["./processor"]

# Final SVE runtime
FROM --platform=linux/arm64/v8 ubuntu:24.04 AS app-sve

ARG ENABLE_SVE
ENV BUILD_VARIANT=sve \
    BUILD_MARCH=armv9-a+sve \
    BUILD_OPTIMIZATION=sve-vectorized \
    ENABLE_SVE=${ENABLE_SVE} \
    SERVICE_PORT=8000

WORKDIR /app

COPY --from=builder-sve /build/processor .

EXPOSE 8000

CMD ["./processor"]
