#!/usr/bin/env bash

# SIMD Visual Benchmark - Comprehensive Performance Testing
# Tests various image sizes and blur radii to validate performance characteristics

set -e

TARGET="${1:-spartikus}"
ITERATIONS=10
OUTPUT_FILE="benchmark_results_$(date +%Y%m%d_%H%M%S).csv"

echo "Starting comprehensive benchmark on target: $TARGET"
echo "Results will be saved to: $OUTPUT_FILE"

# CSV header
echo "implementation,image_size,blur_radius,iterations,process_time_ms,fps,speedup" > "$OUTPUT_FILE"

# Image sizes to test (powers of 2 to test cache boundaries)
SIZES=(128 256 512 1024)

# Blur radii to test (varying compute intensity)
RADII=(3 5 7 10)

# Temporary file to store scalar baselines
BASELINE_FILE=$(mktemp)

echo ""
echo "Running benchmarks..."
echo "===================="

# Function to run a single benchmark
run_benchmark() {
    local impl=$1
    local size=$2
    local radius=$3
    local port=$4

    local url="http://${TARGET}:${port}/process"
    local payload="{\"width\": ${size}, \"height\": ${size}, \"blur_radius\": ${radius}, \"iterations\": ${ITERATIONS}}"

    # Run benchmark and extract timing
    local response=$(curl -s -X POST -H "Content-Type: application/json" -d "$payload" "$url")
    local process_time=$(echo "$response" | python3 -c "import sys, json; d=json.load(sys.stdin); print(d.get('process_time_ms', 'ERROR'))" 2>/dev/null || echo "ERROR")

    if [ "$process_time" == "ERROR" ]; then
        echo "  ⚠️  Failed to get timing data"
        return 1
    fi

    # Calculate FPS
    local fps=$(python3 -c "print(round(1000.0 / $process_time, 2))")

    # Calculate speedup vs scalar baseline
    local key="${size}_${radius}"
    local speedup="1.00"

    if [ "$impl" == "scalar" ]; then
        echo "$key=$process_time" >> "$BASELINE_FILE"
    else
        local baseline=$(grep "^${key}=" "$BASELINE_FILE" 2>/dev/null | cut -d= -f2)
        if [ -n "$baseline" ]; then
            speedup=$(python3 -c "print(round($baseline / $process_time, 2))")
        fi
    fi

    # Output result
    echo "${impl},${size},${radius},${ITERATIONS},${process_time},${fps},${speedup}" >> "$OUTPUT_FILE"

    printf "  %-8s %4dx%-4d r=%2d: %8.3f ms (%6.1f FPS) [%5.2fx]\n" \
        "$impl" "$size" "$size" "$radius" "$process_time" "$fps" "$speedup"
}

# Run all benchmarks
for size in "${SIZES[@]}"; do
    for radius in "${RADII[@]}"; do
        echo ""
        echo "Testing ${size}x${size}, radius ${radius}:"

        # Run scalar first to establish baseline
        run_benchmark "scalar" "$size" "$radius" "8090" || true
        sleep 0.5

        # Run NEON
        run_benchmark "neon" "$size" "$radius" "8091" || true
        sleep 0.5

        # Run SVE
        run_benchmark "sve" "$size" "$radius" "8092" || true
        sleep 0.5
    done
done

echo ""
echo "===================="
echo "Benchmark complete!"
echo "Results saved to: $OUTPUT_FILE"

# Cleanup
rm -f "$BASELINE_FILE"

echo ""
echo "Summary Statistics:"
echo "-------------------"

# Generate summary using Python
python3 <<EOF
import csv
from collections import defaultdict

results = defaultdict(lambda: defaultdict(list))

with open('$OUTPUT_FILE', 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        impl = row['implementation']
        size = int(row['image_size'])
        speedup = float(row['speedup'])
        results[impl][size].append(speedup)

for impl in ['scalar', 'neon', 'sve']:
    print(f"\n{impl.upper()}:")
    for size in [128, 256, 512, 1024]:
        if size in results[impl] and results[impl][size]:
            speedups = results[impl][size]
            avg = sum(speedups) / len(speedups)
            min_s = min(speedups)
            max_s = max(speedups)
            print(f"  {size:4}x{size:<4}: avg {avg:.2f}x  (min {min_s:.2f}x, max {max_s:.2f}x)")
EOF

echo ""
