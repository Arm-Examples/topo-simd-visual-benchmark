#include "simd-loops/helpers.h"
#include "simd-loops/loops.h"

#include "simd-loops/loop_222.c"

loop_function_t __ptr_loop_222 = 0;

void loop_222_convolve(uint64_t m, uint64_t n, uint64_t k,
                       float16_t *kernel,
                       float16_t *values,
                       float16_t *buffer,
                       float16_t *result) {
  struct loop_222_data data;
  data.m = m;
  data.n = n;
  data.k = k;
  data.kernel = kernel;
  data.values = values;
  data.buffer = buffer;
  data.result = result;

  inner_loop_222(&data);
}
