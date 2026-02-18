#include "httplib.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <cstdlib>
#include <stdexcept>

extern "C" {
#ifndef restrict
#define restrict __restrict
#endif
#include "helpers.h"
#undef restrict
}

extern "C" void loop_222_convolve(uint64_t m, uint64_t n, uint64_t k,
                                  float16_t *kernel,
                                  float16_t *values,
                                  float16_t *buffer,
                                  float16_t *result);

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

static inline uint64_t image_border(uint64_t k) {
    return k / 2;
}

static inline uint64_t image_stride(uint64_t k, uint64_t n) {
    return n + k - image_border(k);
}

static inline uint64_t size_of_data(uint64_t m, uint64_t n, uint64_t k) {
    return m * image_stride(k, n) + image_border(k);
}

static inline uint64_t size_of_temp(uint64_t m, uint64_t n, uint64_t k) {
    return n * (m + k);
}

static inline uint64_t align_up(uint64_t value, uint64_t multiple) {
    if (multiple == 0) {
        return value;
    }
    return ((value + multiple - 1) / multiple) * multiple;
}

#if defined(__ARM_FEATURE_SVE) || defined(__ARM_FEATURE_SME2)
static inline uint64_t loop_222_svl_h() {
    uint64_t svl_h = 0;
    asm volatile("cnth %[v]" : [v] "=r"(svl_h)::);
    return svl_h;
    return 0;
}
#endif

static inline uint64_t loop_222_row_inc() {
#if defined(HAVE_AUTOVEC) || defined(HAVE_NATIVE)
    return 4;
#elif defined(__ARM_FEATURE_SME2p1) || defined(__ARM_FEATURE_SME2)
    return loop_222_svl_h() / 2;
#else
    return 1;
#endif
}

static inline uint64_t loop_222_col_inc() {
#if defined(HAVE_AUTOVEC) || defined(HAVE_NATIVE)
    return 1;
#elif defined(__ARM_FEATURE_SVE) || defined(__ARM_FEATURE_SME2)
    return loop_222_svl_h() * 4;
#elif defined(__ARM_NEON)
    return 32;
#else
    return 1;
#endif
}

Image process_image(const Image& src, int radius) {
    if (radius <= 0 || src.width <= 0 || src.height <= 0 || src.channels <= 0 ||
        src.data.empty()) {
        return src;
    }

    uint64_t width = static_cast<uint64_t>(src.width);
    uint64_t height = static_cast<uint64_t>(src.height);
    uint64_t channels = static_cast<uint64_t>(src.channels);
    uint64_t kernel_width = static_cast<uint64_t>(radius) * 2 + 1;

    uint64_t row_inc = loop_222_row_inc();
    uint64_t col_inc = loop_222_col_inc();
    uint64_t padded_width = align_up(width, col_inc);
    uint64_t padded_height = align_up(height, row_inc);

    uint64_t values_size = size_of_data(padded_height, padded_width, kernel_width);
    uint64_t buffer_size = size_of_temp(padded_height, padded_width, kernel_width);
    uint64_t result_size = padded_height * padded_width;

    std::vector<float16_t> kernel(kernel_width);
    FLOAT16_t coeff = static_cast<FLOAT16_t>(1.0f) / static_cast<FLOAT16_t>(kernel_width);
    for (uint64_t i = 0; i < kernel_width; ++i) {
        kernel[i] = native_to_fp16(coeff);
    }

    Image dst;
    dst.width = src.width;
    dst.height = src.height;
    dst.channels = src.channels;
    dst.data.resize(static_cast<size_t>(width * height * channels));

    std::vector<float16_t> values(values_size);
    std::vector<float16_t> buffer(buffer_size);
    std::vector<float16_t> result(result_size);

    float16_t zero_fp16 = native_to_fp16(static_cast<FLOAT16_t>(0.0f));
    uint64_t stride = image_stride(kernel_width, padded_width);
    uint64_t border = image_border(kernel_width);

    for (uint64_t c = 0; c < channels; ++c) {
        std::fill(values.begin(), values.end(), zero_fp16);
        std::fill(buffer.begin(), buffer.end(), zero_fp16);
        std::fill(result.begin(), result.end(), zero_fp16);

        for (uint64_t y = 0; y < height; ++y) {
            float16_t *row_ptr = values.data() + border + y * stride;
            size_t src_row = static_cast<size_t>(y * width * channels);
            for (uint64_t x = 0; x < width; ++x) {
                uint8_t pixel = src.data[src_row + static_cast<size_t>(x * channels + c)];
                row_ptr[x] = native_to_fp16(static_cast<FLOAT16_t>(pixel));
            }
        }

        loop_222_convolve(padded_height, padded_width, kernel_width,
                          kernel.data(), values.data(), buffer.data(), result.data());

        for (uint64_t y = 0; y < height; ++y) {
            size_t dst_row = static_cast<size_t>(y * width * channels);
            const float16_t *row_ptr = result.data() + y * padded_width;
            for (uint64_t x = 0; x < width; ++x) {
                float value = static_cast<float>(fp16_to_native(row_ptr[x]));
                if (value < 0.0f) {
                    value = 0.0f;
                } else if (value > 255.0f) {
                    value = 255.0f;
                }
                dst.data[dst_row + static_cast<size_t>(x * channels + c)] =
                    static_cast<uint8_t>(value + 0.5f);
            }
        }
    }

    return dst;
}

using json = nlohmann::json;
using namespace httplib;

// Convert image to base64-encoded PNG (simplified - just raw RGB for demo)
std::string image_to_base64(const Image& img) {
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string ret;
    const uint8_t* data = img.data.data();
    size_t len = img.data.size();

    int i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while(i++ < 3)
            ret += '=';
    }

    return ret;
}

int main() {
    Server svr;

    // Get build variant from environment
    const char* build_variant = std::getenv("BUILD_VARIANT");
    const char* build_march = std::getenv("BUILD_MARCH");
    const char* build_optimization = std::getenv("BUILD_OPTIMIZATION");

    const char* service_port_env = std::getenv("SERVICE_PORT");

    std::string variant = build_variant ? build_variant : "unknown";
    std::string march = build_march ? build_march : "unknown";
    std::string optimization = build_optimization ? build_optimization : "unknown";
    int service_port = 8000;

    if (service_port_env) {
        try {
            service_port = std::stoi(service_port_env);
        } catch (...) {
            std::cerr << "Invalid SERVICE_PORT '" << service_port_env
                      << "', defaulting to 8000" << std::endl;
            service_port = 8000;
        }
    }

    const char* enable_sve_env = std::getenv("ENABLE_SVE");
    bool sve_enabled = true;
    if (enable_sve_env) {
        std::string flag(enable_sve_env);
        std::transform(flag.begin(), flag.end(), flag.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (flag != "yes") {
            sve_enabled = false;
        }
    }

    if (variant == "sve" && !sve_enabled) {
        std::cout << "ENABLE_SVE disabled for SVE variant; exiting service." << std::endl;
        return 0;
    }

    // Health check endpoint
    svr.Get("/health", [](const Request& req, Response& res) {
        json response = {
            {"status", "healthy"}
        };
        res.set_content(response.dump(), "application/json");
    });

    // Build info endpoint
    svr.Get("/build-info", [&](const Request& req, Response& res) {
        json response = {
            {"variant", variant},
            {"build_march", march},
            {"build_optimization", optimization},
            {"simd_support", {
#ifdef __ARM_NEON
                {"neon", true},
#else
                {"neon", false},
#endif
#ifdef __ARM_FEATURE_SVE
                {"sve", true}
#else
                {"sve", false}
#endif
            }}
        };
        res.set_content(response.dump(), "application/json");
    });

    // Process image endpoint
    svr.Post("/process", [](const Request& req, Response& res) {
        try {
            json body = json::parse(req.body);

            int width = body.value("width", 128);
            int height = body.value("height", 128);
            int blur_radius = body.value("blur_radius", 10);
            int iterations = body.value("iterations", 10);

            // Generate test image
            auto start_total = std::chrono::high_resolution_clock::now();
            Image img = generate_test_image(width, height);

            // Encode original image before processing
            std::string original_image_data = image_to_base64(img);

            // Run multiple iterations for better timing
            auto start_process = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < iterations; i++) {
                img = process_image(img, blur_radius);
            }
            auto end_process = std::chrono::high_resolution_clock::now();

            auto end_total = std::chrono::high_resolution_clock::now();

            // Calculate timings
            double process_time_ms = std::chrono::duration<double, std::milli>(
                end_process - start_process).count() / iterations;
            double total_time_ms = std::chrono::duration<double, std::milli>(
                end_total - start_total).count();

            // Convert image to base64
            std::string image_data = image_to_base64(img);

            json response = {
                {"width", img.width},
                {"height", img.height},
                {"process_time_ms", process_time_ms},
                {"total_time_ms", total_time_ms},
                {"iterations", iterations},
                {"image_data", image_data},
                {"original_image_data", original_image_data}
            };

            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            json error = {
                {"error", e.what()}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // CORS headers
    svr.set_post_routing_handler([](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    svr.Options(".*", [](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    std::cout << "Starting " << variant << " processor service on port "
              << service_port << "..." << std::endl;
    std::cout << "Build flags: " << march << " (" << optimization << ")" << std::endl;

    svr.listen("0.0.0.0", service_port);

    return 0;
}
