# SIMD Visual Benchmark

Visual demonstration of SIMD performance benefits on Arm processors. Compare scalar (no SIMD), NEON (128-bit), and SVE (scalable vector) implementations running identical image processing workloads side-by-side.

Features: NEON, SVE

## Overview

This demo shows real hardware acceleration through three C++ services compiled with different architecture flags, processing the same box blur algorithm on images. Performance differences are measured in real-time and displayed in an interactive web dashboard.

## Components

### processor-runtime/
C++ image processing service built in three variants:
- **Scalar**: No SIMD optimizations (`-march=armv8-a+nosimd`)
- **NEON**: 128-bit SIMD vectors (`-march=armv8-a`)
- **SVE**: Scalable Vector Extension (`-march=armv9-a+sve`)

### dashboard-ui/
Python FastAPI web application providing interactive controls and side-by-side benchmark results.

## Prerequisites

1. **Arm64 host** - Required for building and running the containers
2. **Docker with BuildKit** - For multi-stage builds
3. **SVE-capable hardware** (optional) - For seeing actual SVE benefits

## Usage

The easiest way to deploy is using `topo`. Download and install `topo` from [here](https://github.com/arm/topo)

### Clone the project:
```bash
topo clone simd-visual-benchmark <url-to-repo>
```

Topo uses [remoteproc-runtime](https://github.com/arm/remoteproc-runtime) to deploy containers to remote processors.
If it is not already installed, you can install it using topo:
```bash
topo install remoteproc-runtime --target <ip-address-of-target>
```

### Build and Deploy the project:
```bash
cd simd-visual-benchmark
topo deploy --target <ip-address-of-target>
```

Then visit: **http://localhost:8095**
