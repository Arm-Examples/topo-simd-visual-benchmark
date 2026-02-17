#include "httplib.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cstdlib>
#include <stdexcept>

// Forward declarations from image_processor.cpp
struct Image {
    std::vector<uint8_t> data;
    int width;
    int height;
    int channels;
};

Image generate_test_image(int width, int height);
Image process_image(const Image& src, int radius);

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
                {"image_data", image_data}
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
