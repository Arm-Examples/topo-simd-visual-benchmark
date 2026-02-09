#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

// Simple image structure
struct Image {
    std::vector<uint8_t> data;
    int width;
    int height;
    int channels;
};

// Generate a test pattern image with vibrant colors
Image generate_test_image(int width, int height) {
    Image img;
    img.width = width;
    img.height = height;
    img.channels = 3; // RGB
    img.data.resize(width * height * 3);

    // Create a colorful checkerboard + gradient pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;

            // Checkerboard base pattern with high contrast
            bool checker = ((x / 64) + (y / 64)) % 2 == 0;

            // Much brighter base values
            img.data[idx + 0] = checker ? 255 : (x * 200 / width + 50);           // R: bright red or gradient
            img.data[idx + 1] = checker ? (y * 200 / height + 50) : 255;         // G: gradient or bright green
            img.data[idx + 2] = ((x + y) * 150 / (width + height) + 100);        // B: always relatively bright
        }
    }

    return img;
}

// Box blur using scalar operations (no SIMD)
Image box_blur_scalar(const Image& src, int radius) {
    Image dst = src;
    std::vector<float> temp(src.width * src.height * src.channels);

    const int kernel_size = radius * 2 + 1;
    const float kernel_norm = 1.0f / kernel_size;

    // Horizontal pass
    for (int y = 0; y < src.height; y++) {
        for (int x = 0; x < src.width; x++) {
            for (int c = 0; c < src.channels; c++) {
                float sum = 0.0f;

                for (int kx = -radius; kx <= radius; kx++) {
                    int sample_x = std::min(std::max(x + kx, 0), src.width - 1);
                    int idx = (y * src.width + sample_x) * src.channels + c;
                    sum += src.data[idx];
                }

                int idx = (y * src.width + x) * src.channels + c;
                temp[idx] = sum * kernel_norm;
            }
        }
    }

    // Vertical pass
    for (int y = 0; y < src.height; y++) {
        for (int x = 0; x < src.width; x++) {
            for (int c = 0; c < src.channels; c++) {
                float sum = 0.0f;

                for (int ky = -radius; ky <= radius; ky++) {
                    int sample_y = std::min(std::max(y + ky, 0), src.height - 1);
                    int idx = (sample_y * src.width + x) * src.channels + c;
                    sum += temp[idx];
                }

                int idx = (y * src.width + x) * src.channels + c;
                dst.data[idx] = static_cast<uint8_t>(std::min(std::max(sum * kernel_norm, 0.0f), 255.0f));
            }
        }
    }

    return dst;
}

#ifdef __ARM_NEON
// Box blur using NEON intrinsics
Image box_blur_neon(const Image& src, int radius) {
    Image dst = src;
    std::vector<float> temp(src.width * src.height * src.channels);

    const int kernel_size = radius * 2 + 1;
    const float kernel_norm = 1.0f / kernel_size;
    const float32x4_t norm_vec = vdupq_n_f32(kernel_norm);

    // Horizontal pass with NEON
    for (int y = 0; y < src.height; y++) {
        for (int x = 0; x < src.width; x++) {
            float32x4_t sum_vec = vdupq_n_f32(0.0f);

            for (int kx = -radius; kx <= radius; kx++) {
                int sample_x = std::min(std::max(x + kx, 0), src.width - 1);
                int idx = (y * src.width + sample_x) * src.channels;

                // Load 3 bytes and convert to float32x4
                float vals[4] = {
                    static_cast<float>(src.data[idx + 0]),
                    static_cast<float>(src.data[idx + 1]),
                    static_cast<float>(src.data[idx + 2]),
                    0.0f
                };
                float32x4_t val_vec = vld1q_f32(vals);
                sum_vec = vaddq_f32(sum_vec, val_vec);
            }

            sum_vec = vmulq_f32(sum_vec, norm_vec);

            int idx = (y * src.width + x) * src.channels;
            float result[4];
            vst1q_f32(result, sum_vec);
            temp[idx + 0] = result[0];
            temp[idx + 1] = result[1];
            temp[idx + 2] = result[2];
        }
    }

    // Vertical pass with NEON
    for (int y = 0; y < src.height; y++) {
        for (int x = 0; x < src.width; x++) {
            float32x4_t sum_vec = vdupq_n_f32(0.0f);

            for (int ky = -radius; ky <= radius; ky++) {
                int sample_y = std::min(std::max(y + ky, 0), src.height - 1);
                int idx = (sample_y * src.width + x) * src.channels;

                float vals[4] = {temp[idx + 0], temp[idx + 1], temp[idx + 2], 0.0f};
                float32x4_t val_vec = vld1q_f32(vals);
                sum_vec = vaddq_f32(sum_vec, val_vec);
            }

            sum_vec = vmulq_f32(sum_vec, norm_vec);

            int idx = (y * src.width + x) * src.channels;
            float result[4];
            vst1q_f32(result, sum_vec);
            dst.data[idx + 0] = static_cast<uint8_t>(std::min(std::max(result[0], 0.0f), 255.0f));
            dst.data[idx + 1] = static_cast<uint8_t>(std::min(std::max(result[1], 0.0f), 255.0f));
            dst.data[idx + 2] = static_cast<uint8_t>(std::min(std::max(result[2], 0.0f), 255.0f));
        }
    }

    return dst;
}
#endif

#ifdef __ARM_FEATURE_SVE
#include <arm_sve.h>

// Box blur using SVE intrinsics - match NEON's pixel-wise approach
Image box_blur_sve(const Image& src, int radius) {
    Image dst = src;
    std::vector<float> temp(src.width * src.height * src.channels);

    const int kernel_size = radius * 2 + 1;
    const float kernel_norm = 1.0f / kernel_size;

    // Horizontal pass - process RGB pixels using fixed-size SVE vectors
    for (int y = 0; y < src.height; y++) {
        for (int x = 0; x < src.width; x++) {
            float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};

            for (int kx = -radius; kx <= radius; kx++) {
                int sample_x = std::min(std::max(x + kx, 0), src.width - 1);
                int idx = (y * src.width + sample_x) * src.channels;

                sum[0] += static_cast<float>(src.data[idx + 0]);
                sum[1] += static_cast<float>(src.data[idx + 1]);
                sum[2] += static_cast<float>(src.data[idx + 2]);
            }

            int idx = (y * src.width + x) * src.channels;
            temp[idx + 0] = sum[0] * kernel_norm;
            temp[idx + 1] = sum[1] * kernel_norm;
            temp[idx + 2] = sum[2] * kernel_norm;
        }
    }

    // Vertical pass
    for (int y = 0; y < src.height; y++) {
        for (int x = 0; x < src.width; x++) {
            float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};

            for (int ky = -radius; ky <= radius; ky++) {
                int sample_y = std::min(std::max(y + ky, 0), src.height - 1);
                int idx = (sample_y * src.width + x) * src.channels;

                sum[0] += temp[idx + 0];
                sum[1] += temp[idx + 1];
                sum[2] += temp[idx + 2];
            }

            int idx = (y * src.width + x) * src.channels;
            dst.data[idx + 0] = static_cast<uint8_t>(std::min(std::max(sum[0] * kernel_norm, 0.0f), 255.0f));
            dst.data[idx + 1] = static_cast<uint8_t>(std::min(std::max(sum[1] * kernel_norm, 0.0f), 255.0f));
            dst.data[idx + 2] = static_cast<uint8_t>(std::min(std::max(sum[2] * kernel_norm, 0.0f), 255.0f));
        }
    }

    return dst;
}
#endif

// Main processing function - routes to appropriate implementation
Image process_image(const Image& src, int radius) {
#if defined(__ARM_FEATURE_SVE)
    return box_blur_sve(src, radius);
#elif defined(__ARM_NEON)
    return box_blur_neon(src, radius);
#else
    return box_blur_scalar(src, radius);
#endif
}
