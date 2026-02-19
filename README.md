# SIMD Visual Benchmark

Visual demonstration of SIMD performance benefits on Arm processors. Compare scalar (no SIMD), NEON (128-bit), and SVE (scalable vector) implementations running identical image processing workloads side-by-side.

It demonstrates:

- Use of multi-stage docker builds with Topo
- Running and profiling hardware acceleration features with a simple image box blur algorithm
- An interactive web dashboard to run and view the benchmark results

To find out more about the Topo template format, see [arm/topo-template-format](https://github.com/arm/topo-template-format)

## Usage

To use this template download and install `topo` from [arm/topo](https://github.com/arm/topo)

### Clone the project:

```bash
topo clone ./target-directory template:simd-visual-benchmark
```

You will be prompted to provide values for the template parameters.

### Build and Deploy the project:

```bash
cd target-directory
topo deploy --target <ip-address-of-target>
```

### What you will see

Once deployment completes, open a browser to `http://<ip-address-of-target>:8095`, click "Run" in the top right and you'll

![screenshot of the webpage showing a greeting for Clark Kent](./.screenshot.png)

# Acknowledgments

This template makes use of Arm's [simd-loops](https://gitlab.arm.com/architecture/simd-loops) project to perform the hardware-accelerated convolution.
