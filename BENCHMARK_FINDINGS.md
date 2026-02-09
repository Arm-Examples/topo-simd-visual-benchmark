# SIMD Visual Benchmark - Performance Analysis

## Executive Summary

Comprehensive benchmarking across 16 configurations (4 image sizes × 4 blur radii) revealed **surprising and counterintuitive performance characteristics** that challenge conventional wisdom about SIMD optimization.

**Key Finding**: SIMD performance is highly sensitive to image size, with unexpected "sweet spots" and "dead zones" that don't align with simple compute-bound vs. memory-bound models.

## Test Configuration

- **Target**: Neoverse-V2 (256-bit SVE, 128-bit NEON)
- **Image Sizes**: 128, 256, 512, 1024 pixels
- **Blur Radii**: 3, 5, 7, 10 pixels
- **Iterations**: 10 (for timing stability)
- **Total Tests**: 48 (16 configurations × 3 implementations)

## Performance Summary by Image Size

### 128×128 (16KB raw, ~65KB with temps)
**Status**: ✅ **Best SIMD showcase**

| Radius | Scalar | NEON   | SVE    |
|--------|--------|--------|--------|
| 3      | 1.00x  | 1.52x  | **2.99x** |
| 5      | 1.00x  | 0.76x  | 0.71x  |
| 7      | 1.00x  | 1.48x  | 1.42x  |
| 10     | 1.00x  | **1.57x** | 1.44x  |

**Analysis**:
- At radius 3, SVE achieves nearly **3x speedup** - the best result in all tests!
- Radius 5 shows a surprising **scalar win** (SIMD 30% slower)
- Radii 7-10 show healthy SIMD advantages (1.4-1.6x)

**Recommended Default**: ✅ **128×128, radius 10** (NEON 1.57x, SVE 1.44x)

### 256×256 (192KB raw, ~750KB with temps)
**Status**: ⚠️ **SIMD struggles - scalar wins consistently**

| Radius | Scalar | NEON   | SVE    |
|--------|--------|--------|--------|
| 3      | 1.00x  | 0.77x  | 0.68x  |
| 5      | 1.00x  | 0.76x  | 0.71x  |
| 7      | 1.00x  | 0.75x  | 0.72x  |
| 10     | 1.00x  | 0.75x  | 0.70x  |

**Analysis**:
- Both NEON and SVE are **25-32% slower** than scalar across all radii
- This size should fit comfortably in L2 cache but shows worst SIMD performance
- Results are highly consistent (0.70-0.77x) across different radii

**Hypothesis**:
- Loop structure at this exact dimension may interact poorly with compiler vectorization
- Possible cache line alignment issues (256-byte stride)
- Hardware prefetcher confusion with this specific access pattern

### 512×512 (768KB raw, ~3MB with temps)
**Status**: ⚖️ **Performance convergence - minimal difference**

| Radius | Scalar | NEON   | SVE    |
|--------|--------|--------|--------|
| 3      | 1.00x  | 0.85x  | 0.83x  |
| 5      | 1.00x  | 0.94x  | 0.96x  |
| 7      | 1.00x  | 1.00x  | **1.05x** |
| 10     | 1.00x  | 1.05x  | **1.09x** |

**Analysis**:
- Working set exceeds typical L2 cache (512KB-2MB)
- Performance converges as workload becomes memory-bound
- SVE shows slight advantage at larger radii (5-9% speedup)

### 1024×1024 (3MB raw, ~12MB with temps)
**Status**: ✅ **SIMD wins despite memory-bound workload**

| Radius | Scalar | NEON   | SVE    |
|--------|--------|--------|--------|
| 3      | 1.00x  | 1.22x  | **1.53x** |
| 5      | 1.00x  | 1.23x  | **1.34x** |
| 7      | 1.00x  | 1.23x  | **1.39x** |
| 10     | 1.00x  | 1.30x  | **1.42x** |

**Analysis**:
- Despite being clearly memory-bound, SIMD shows consistent advantages
- SVE outperforms NEON across all configurations (1.34-1.53x vs 1.22-1.30x)
- Larger images favor SIMD's better memory bandwidth utilization

**Why SIMD wins when memory-bound**:
- Better hardware prefetcher interaction with SIMD's predictable patterns
- More efficient memory streaming in compiler-generated SIMD code
- Neoverse-V2's wider SVE vectors (256-bit) provide bandwidth advantages

## Validated vs. Rejected Hypotheses

### ✅ Validated
1. **Large images favor SIMD**: At 1024×1024, consistent 1.2-1.5x speedups
2. **SVE generally matches/exceeds NEON**: Especially at 1024×1024 (1.3-1.5x vs 1.2-1.3x)
3. **Radius affects performance**: Larger radii generally favor SIMD (more compute per pixel)

### ❌ Rejected
1. **"256×256 is the compute-bound sweet spot"**: Completely wrong - worst SIMD performance!
2. **"Memory-bound = SIMD loses"**: 1024×1024 shows strong SIMD advantages despite being memory-bound
3. **"Simple cache size model"**: Performance doesn't correlate linearly with cache boundaries

## Real-World Lessons

1. **Measurement is essential**: Our intuition about "compute-bound sweet spots" was completely wrong
2. **Size-specific tuning matters**: Different image dimensions have wildly different optimal strategies
3. **SIMD isn't always faster**: 256×256 shows 25-30% slowdowns with SIMD
4. **Memory-bound ≠ SIMD loses**: Large images still benefit from SIMD's better memory patterns
5. **Compiler vectorization varies**: `-ftree-vectorize` helps at some sizes, hurts at others

## Recommendations

### For This Demo
**Default Configuration**: 128×128, radius 10
- NEON: 1.57x speedup
- SVE: 1.44x speedup
- Fast enough for interactive demos (1.8ms)
- Clearly demonstrates SIMD benefits

### For Real-World Applications
1. **Always benchmark your specific use case** - performance is highly sensitive to dimensions
2. **Consider multiple implementations** - scalar may be faster at certain sizes
3. **Profile with real data** - cache effects depend on actual memory access patterns
4. **Test across target hardware** - results may vary significantly on different CPUs

## Full Dataset

Complete results available in: `benchmark_results_20251203_093145.csv`

Raw data includes:
- Process time (ms)
- FPS
- Speedup vs scalar baseline
- All 48 test configurations

## Conclusion

SIMD optimization is **far more nuanced** than "always faster with larger vectors." The 256×256 anomaly demonstrates that:
- Compiler vectorization can hurt performance in specific cases
- Cache and memory access patterns matter more than vector width
- Real-world optimization requires empirical measurement, not assumptions

This benchmark showcases both the **power and limitations** of SIMD, making it an excellent educational tool for understanding hardware acceleration trade-offs.
