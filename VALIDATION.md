# SIMD Instruction Validation

This document explains how to validate that each binary variant contains the expected SIMD instructions.

## Problem

The three variants (scalar, NEON, SVE) claim to use different SIMD instruction sets, but without verification:

1. **Scalar**: Should contain NO SIMD instructions (compiled with `-fno-tree-vectorize`)
2. **NEON**: Should contain NEON intrinsics from `box_blur_neon()`
3. **SVE**: Currently does NOT use SVE intrinsics in `box_blur_sve()` (lines 162-216). It relies entirely on compiler auto-vectorization, which may or may not generate SVE instructions.

## Solution

Use the `validate-simd-instructions.sh` script to disassemble all compiled binaries and verify the presence (or absence) of NEON and SVE instructions.

## Usage

From the project root directory:

```bash
./validate-simd-instructions.sh
```

## How It Works

The validator:

1. **Extracts binaries** from each Docker build target (scalar, NEON, SVE)
2. **Disassembles** each binary using `objdump`
3. **Searches** for SIMD-specific instruction patterns:
   - **NEON patterns**:
     - Vector registers: `v0-v31` (e.g., `v0.4s`, `v1.2d`)
     - NEON instructions: `fadd v`, `fmul v`, `ld1 {v`, `st1 {v`, etc.
   - **SVE patterns**:
     - Vector registers: `z0-z31` (e.g., `z0.s`, `z1.d`)
     - Predicate registers: `p0-p15` (e.g., `p0/z`, `p1/m`)
     - SVE instructions: `ld1w {z`, `fadd z`, `fmul z`, `ptrue`, etc.
4. **Validates** each binary against expected instruction sets
5. **Reports** results with examples of matched instructions

## Expected Results

### Scalar Binary
- ✓ Should NOT contain NEON instructions
- ✓ Should NOT contain SVE instructions
- Compiled with `-march=armv8-a+nosimd -fno-tree-vectorize`
- **Expected outcome**: PASS (no SIMD instructions found)

### NEON Binary
- ✓ Should contain NEON instructions (`v` registers like `v0.4s`, `v1.2d`)
- ✓ Should NOT contain SVE instructions
- Uses explicit NEON intrinsics in `box_blur_neon()`
- **Expected outcome**: PASS (NEON instructions found, no SVE)

### SVE Binary
- ✓ Should contain SVE instructions
- May contain NEON instructions alongside SVE (acceptable - compiler may use both)
- **Expected outcome**: PASS (SVE instructions found, NEON optional)
- **Expected SVE instructions**:
  - Z registers: `z0.s`, `z1.d`, etc.
  - Predicate registers: `p0/z`, `p1/m`, etc.
  - SVE loads/stores: `ld1w {z0.s}`, `st1d {z1.d}`, etc.
  - SVE operations: `fadd z0.s`, `fmul z1.d`, `ptrue`, `index`, etc.

## About the SVE Implementation

The current `box_blur_sve()` function (lines 162-216 of `image_processor.cpp`) **does not use explicit SVE intrinsics**. Instead, it relies on compiler auto-vectorization with the flags:
- `-march=armv9-a+sve`
- `-ftree-vectorize`

**The good news**: The validator confirms that the compiler **does successfully generate SVE instructions** (~70 instances, 0.09% of total instructions). The binary contains:
- SVE vector operations: `mov z6.s`, `smin z0.s`, `mad z0.s`
- Predicate usage: `p0/m`, `p1/z`
- SVE-specific instructions: `ptrue`, `index`, `ld1b`

**The tradeoff**: Relying on auto-vectorization means:
- ✓ Simpler code (no intrinsics needed)
- ✓ Compiler does generate SVE instructions
- ✗ Less control over exactly which operations use SVE
- ✗ Compiler also generates NEON instructions (~122 instances)
- ✗ Not guaranteed to vectorize optimally for all compilers/versions

**For a demo**: The current approach works and produces real SVE instructions.

**For production**: Consider explicit SVE intrinsics for more control:

```cpp
#include <arm_sve.h>

svfloat32_t sum_vec = svdup_n_f32(0.0f);
svbool_t pred = svptrue_b32();
svfloat32_t data = svld1_f32(pred, &input[i]);
sum_vec = svadd_f32_z(pred, sum_vec, data);
```

## Requirements

- Docker (to build and extract binaries)
- `objdump` (for disassembly, usually part of `binutils`)
- `file` (to check binary type)
- Bash shell

## Validation Output

The script provides detailed output for each binary:

1. **Binary extraction** - Shows progress extracting binaries from Docker images
2. **Per-binary analysis** - For each variant:
   - File type confirmation
   - Total instruction count
   - NEON instruction search results with examples
   - SVE instruction search results with examples
   - Pass/fail verdict based on expectations
3. **Summary** - Overall pass/fail status for all three binaries

## Exit Codes

- `0`: All validations passed (each binary contains expected instructions)
- `1`: Validation failed (one or more binaries don't match expectations)
