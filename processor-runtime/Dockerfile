# Multi-stage Dockerfile for building scalar, NEON, and SVE variants
ARG BUILDER_LAYER=ubuntu:24.04

# Base builder stage with common dependencies
FROM ubuntu:24.04 AS builder-base

ARG BUILD_MARCH
ENV BUILD_MARCH=${BUILD_MARCH}

RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    clang-20

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

# Copy simd-loops files
COPY simd-loops/ ./simd-loops/

# Copy source files
COPY server.cpp loop_222_wrapper.c ./ 

# Scalar variant - no SIMD optimizations
FROM builder-base AS builder-scalar

ENV FEAT_DEFS="-DHAVE_NATIVE -U__ARM_NEON -U__ARM_FEATURE_SVE -U__ARM_FEATURE_SVE2 -U__ARM_FEATURE_SVE2p1 -U__ARM_FEATURE_SME -U__ARM_FEATURE_SME2 -U__ARM_FEATURE_SME2p1"
ENV COMMON_DEFS="-DMAX_GLOBAL_MEMORY_KIB=16384 -DPROBLEM_SIZE_LIMIT_KIB=64 -DMAX_VL=512 -MMD -Wall -Werror -O2 -fno-tree-vectorize"
ENV CFLAGS="-march=${BUILD_MARCH} ${COMMON_DEFS} ${FEAT_DEFS}"
ENV CXXFLAGS="${CFLAGS} -std=c++17"
ENV CC=clang-20 \
    CXX=clang++-20 \
    STRIP=strip

RUN ${CC} ${CFLAGS} -I/usr/local/include -I. -c simd-loops/helpers.c -o helpers.o && \
    ${CC} ${CFLAGS} -I/usr/local/include -I. -c simd-loops/static_rand.c -o static_rand.o && \
    ${CC} ${CFLAGS} -I/usr/local/include -I. -c loop_222_wrapper.c -o loop_222_wrapper.o && \
    ${CXX} ${CXXFLAGS} -I/usr/local/include -I. -c server.cpp -o server.o && \
    ${CXX} -o processor server.o helpers.o static_rand.o loop_222_wrapper.o \
    && ${STRIP} processor

# NEON variant - NEON optimizations enabled
FROM builder-base AS builder-neon

ENV FEAT_DEFS="-D__ARM_NEON -U__ARM_FEATURE_SVE -U__ARM_FEATURE_SVE2 -U__ARM_FEATURE_SVE2p1 -U__ARM_FEATURE_SME -U__ARM_FEATURE_SME2 -U__ARM_FEATURE_SME2p1"
ENV COMMON_DEFS="-DMAX_GLOBAL_MEMORY_KIB=16384 -DPROBLEM_SIZE_LIMIT_KIB=64 -DMAX_VL=512 -MMD -Wall -Werror -O2 -fno-tree-vectorize"
ENV CFLAGS="-march=${BUILD_MARCH} ${COMMON_DEFS} ${FEAT_DEFS}"
ENV CXXFLAGS="${CFLAGS} -std=c++17"
ENV CC=clang-20 \
    CXX=clang++-20 \
    STRIP=strip

RUN ${CC} ${CFLAGS} -I/usr/local/include -I. -c simd-loops/helpers.c -o helpers.o && \
    ${CC} ${CFLAGS} -I/usr/local/include -I. -c simd-loops/static_rand.c -o static_rand.o && \
    ${CC} ${CFLAGS} -I/usr/local/include -I. -c loop_222_wrapper.c -o loop_222_wrapper.o && \
    ${CXX} ${CXXFLAGS} -I/usr/local/include -I. -c server.cpp -o server.o && \
    ${CXX} -o processor server.o helpers.o static_rand.o loop_222_wrapper.o \
    && ${STRIP} processor

# SVE variant - SVE optimizations enabled
FROM builder-base AS builder-sve

ENV FEAT_DEFS="-D__ARM_FEATURE_SVE2 -U__ARM_FEATURE_SVE2p1 -U__ARM_FEATURE_SME -U__ARM_FEATURE_SME2 -U__ARM_FEATURE_SME2p1"
ENV COMMON_DEFS="-DMAX_GLOBAL_MEMORY_KIB=16384 -DPROBLEM_SIZE_LIMIT_KIB=64 -DMAX_VL=512 -MMD -Wall -Werror -O2 -fno-tree-vectorize"
ENV CFLAGS="-march=${BUILD_MARCH} ${COMMON_DEFS} ${FEAT_DEFS}"
ENV CXXFLAGS="${CFLAGS} -std=c++17"
ENV CC=clang-20 \
    CXX=clang++-20 \
    STRIP=strip

RUN ${CC} ${CFLAGS} -I/usr/local/include -I. -c simd-loops/helpers.c -o helpers.o && \
    ${CC} ${CFLAGS} -I/usr/local/include -I. -c simd-loops/static_rand.c -o static_rand.o && \
    ${CC} ${CFLAGS} -I/usr/local/include -I. -c loop_222_wrapper.c -o loop_222_wrapper.o && \
    ${CXX} ${CXXFLAGS} -I/usr/local/include -I. -c server.cpp -o server.o && \
    ${CXX} -o processor server.o helpers.o static_rand.o loop_222_wrapper.o \
    && ${STRIP} processor

# Final app stage
FROM ${BUILDER_LAYER} AS app

ARG ENABLE_SVE

ENV ENABLE_SVE=${ENABLE_SVE} \
    SERVICE_PORT=8000

EXPOSE 8000

CMD ["/build/processor"]
