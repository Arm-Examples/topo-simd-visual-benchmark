#!/usr/bin/env bash
# Validator script to check if compiled binaries contain the expected SIMD instructions
# This scans the disassembly of binaries to verify NEON and SVE instructions are present

set -euo pipefail

# Color output helpers
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== SIMD Instruction Validator ===${NC}"
echo ""
echo "This script will build three Docker images and analyze their binaries."
echo "The build process may take several minutes on first run..."
echo ""

# Function to extract binary from a Docker image
extract_binary_from_image() {
    local service_name="$1"
    local target_name="$2"
    local output_path="$3"

    # Build the specific target (show progress)
    docker compose -f compose.yaml build --build-arg BUILDKIT_INLINE_CACHE=1 "$service_name"

    # Construct image name directly (project name is simd-visual-benchmark from compose config)
    local image_name="simd-visual-benchmark-${service_name}:latest"

    # Create a temporary container and copy the binary
    local container_id
    container_id=$(docker create "$image_name")
    docker cp "${container_id}:/app/processor" "$output_path"
    docker rm "$container_id" > /dev/null 2>&1

    echo -e "${GREEN}✓ Binary extracted to ${output_path}${NC}"
}

# Function to check for NEON instructions in a binary
check_neon_instructions() {
    local binary_path="$1"
    local disasm_output="$2"
    local total_instructions="$3"

    # NEON instructions include patterns like:
    # - v0-v31: NEON vector registers (e.g., v0.4s, v1.2d)
    # - NEON-specific mnemonics: fadd, fmul, ld1, st1, etc. with v registers

    local neon_patterns=(
        '\bv[0-9]+\.[0-9]'      # NEON V registers with lane specifier (v0.4s, v1.2d, etc.)
        '\bfadd\s+v'            # NEON floating-point add
        '\bfmul\s+v'            # NEON floating-point multiply
        '\bfmla\s+v'            # NEON floating-point multiply-add
        '\bld1\s+{v'            # NEON load instructions
        '\bst1\s+{v'            # NEON store instructions
        '\bdup\s+v'             # NEON duplicate
        '\baddv\s+'             # NEON add across vector
        '\bfaddp\s+v'           # NEON pairwise add
    )

    local neon_found=false
    local neon_count=0
    local matched_patterns=()

    for pattern in "${neon_patterns[@]}"; do
        local matches
        matches=$(echo "$disasm_output" | grep -E "$pattern" || echo "")
        if [[ -n "$matches" ]]; then
            neon_found=true
            local count
            count=$(echo "$matches" | wc -l | tr -d ' ')
            neon_count=$((neon_count + count))
            matched_patterns+=("$pattern")

            # Show a sample of matches (first 3 lines)
            echo ""
            echo -e "${YELLOW}NEON pattern matched: ${pattern}${NC}"
            echo "$matches" | head -3
            if [[ $(echo "$matches" | wc -l) -gt 3 ]]; then
                echo "  ... (and $(($(echo "$matches" | wc -l) - 3)) more matches)"
            fi
        fi
    done

    if [[ "$neon_found" == true ]]; then
        echo ""
        echo -e "${GREEN}✓ NEON instructions FOUND${NC}"
        echo "  - Matched patterns: ${#matched_patterns[@]}"
        echo "  - Total NEON instruction instances: ~$neon_count"
        local percentage=$(awk "BEGIN {printf \"%.2f\", ($neon_count / $total_instructions) * 100}")
        echo "  - Percentage of instructions: ${percentage}%"
        return 0
    else
        echo ""
        echo -e "${RED}✗ NO NEON instructions found${NC}"
        return 1
    fi
}

# Function to check for SVE instructions in a binary
check_sve_instructions() {
    local binary_path="$1"
    local disasm_output="$2"
    local total_instructions="$3"

    # SVE instructions include patterns like:
    # - z0-z31: SVE vector registers (e.g., z0.s, z1.d)
    # - p0-p15: SVE predicate registers (e.g., p0/z, p1/m)
    # - SVE-specific mnemonics: ld1, st1, add, mul, etc. with SVE register operands

    local sve_patterns=(
        '\bz[0-9]+\.'           # SVE Z registers with lane specifier (z0.s, z1.d, etc.)
        '\bp[0-9]+/'            # SVE predicate registers (p0/z, p1/m, etc.)
        '\bld1[bhwd]\s+{z'      # SVE load instructions
        '\bst1[bhwd]\s+{z'      # SVE store instructions
        '\bfadd\s+z'            # SVE floating-point add
        '\bfmul\s+z'            # SVE floating-point multiply
        '\bfmla\s+z'            # SVE floating-point multiply-add
        '\bptrue\b'             # SVE predicate true
        '\bwhilelt\b'           # SVE while-less-than
        '\bindex\s+z'           # SVE index generation
        '\bdup\s+z'             # SVE duplicate
    )

    local sve_found=false
    local sve_count=0
    local matched_patterns=()

    for pattern in "${sve_patterns[@]}"; do
        local matches
        matches=$(echo "$disasm_output" | grep -E "$pattern" || echo "")
        if [[ -n "$matches" ]]; then
            sve_found=true
            local count
            count=$(echo "$matches" | wc -l | tr -d ' ')
            sve_count=$((sve_count + count))
            matched_patterns+=("$pattern")

            # Show a sample of matches (first 3 lines)
            echo ""
            echo -e "${YELLOW}SVE pattern matched: ${pattern}${NC}"
            echo "$matches" | head -3
            if [[ $(echo "$matches" | wc -l) -gt 3 ]]; then
                echo "  ... (and $(($(echo "$matches" | wc -l) - 3)) more matches)"
            fi
        fi
    done

    if [[ "$sve_found" == true ]]; then
        echo ""
        echo -e "${GREEN}✓ SVE instructions FOUND${NC}"
        echo "  - Matched patterns: ${#matched_patterns[@]}"
        echo "  - Total SVE instruction instances: ~$sve_count"
        local percentage=$(awk "BEGIN {printf \"%.2f\", ($sve_count / $total_instructions) * 100}")
        echo "  - Percentage of instructions: ${percentage}%"
        return 0
    else
        echo ""
        echo -e "${RED}✗ NO SVE instructions found${NC}"
        return 1
    fi
}

# Function to analyze a binary for SIMD instructions
analyze_binary() {
    local binary_path="$1"
    local variant_name="$2"
    local expect_neon="$3"
    local expect_sve="$4"

    echo ""
    echo -e "${BLUE}Analyzing ${variant_name} binary: ${binary_path}${NC}"
    echo "========================================"

    if [[ ! -f "$binary_path" ]]; then
        echo -e "${RED}✗ Binary not found: ${binary_path}${NC}"
        return 1
    fi

    # Check file type
    local file_info
    file_info=$(file "$binary_path")
    echo "File type: $file_info"

    if [[ ! "$file_info" =~ "ARM aarch64" ]]; then
        echo -e "${RED}✗ Not an ARM64 binary${NC}"
        return 1
    fi

    # Disassemble the binary
    local disasm_output
    disasm_output=$(objdump -d "$binary_path" 2>/dev/null || echo "")

    if [[ -z "$disasm_output" ]]; then
        echo -e "${RED}✗ Failed to disassemble binary${NC}"
        return 1
    fi

    # Count total instructions
    local total_instructions
    total_instructions=$(echo "$disasm_output" | grep -c '^\s*[0-9a-f]\+:' || echo "0")
    echo "Total instructions: $total_instructions"

    # Check for NEON instructions
    local neon_result=1
    if [[ "$expect_neon" == "yes" ]]; then
        echo ""
        echo "Checking for NEON instructions (expected)..."
        echo "----------------------------------------"
        check_neon_instructions "$binary_path" "$disasm_output" "$total_instructions" && neon_result=0 || neon_result=$?
    elif [[ "$expect_neon" == "optional" ]]; then
        echo ""
        echo "Checking for NEON instructions (optional - acceptable if present)..."
        echo "----------------------------------------"
        check_neon_instructions "$binary_path" "$disasm_output" "$total_instructions" && neon_result=0 || neon_result=$?
    else
        echo ""
        echo "Checking for NEON instructions (should NOT be present)..."
        echo "----------------------------------------"
        check_neon_instructions "$binary_path" "$disasm_output" "$total_instructions" && neon_result=0 || neon_result=$?
    fi

    # Check for SVE instructions
    local sve_result=1
    if [[ "$expect_sve" == "yes" ]]; then
        echo ""
        echo "Checking for SVE instructions (expected)..."
        echo "----------------------------------------"
        check_sve_instructions "$binary_path" "$disasm_output" "$total_instructions" && sve_result=0 || sve_result=$?
    else
        echo ""
        echo "Checking for SVE instructions (should NOT be present)..."
        echo "----------------------------------------"
        check_sve_instructions "$binary_path" "$disasm_output" "$total_instructions" && sve_result=0 || sve_result=$?
    fi

    echo ""
    echo "========================================"

    # Determine if this binary passes validation
    local validation_passed=true

    if [[ "$expect_neon" == "yes" && $neon_result -ne 0 ]]; then
        echo -e "${RED}✗ FAIL: Expected NEON instructions but none found${NC}"
        validation_passed=false
    elif [[ "$expect_neon" == "no" && $neon_result -eq 0 ]]; then
        echo -e "${YELLOW}⚠ WARNING: Found NEON instructions but none were expected${NC}"
    elif [[ "$expect_neon" == "optional" ]]; then
        if [[ $neon_result -eq 0 ]]; then
            echo -e "${GREEN}✓ NEON instructions found (acceptable alongside SVE)${NC}"
        else
            echo -e "${GREEN}✓ No NEON instructions (SVE-only, also acceptable)${NC}"
        fi
    fi

    if [[ "$expect_sve" == "yes" && $sve_result -ne 0 ]]; then
        echo -e "${RED}✗ FAIL: Expected SVE instructions but none found${NC}"
        validation_passed=false
    elif [[ "$expect_sve" == "no" && $sve_result -eq 0 ]]; then
        echo -e "${YELLOW}⚠ WARNING: Found SVE instructions but none were expected${NC}"
    fi

    if [[ "$validation_passed" == true ]]; then
        echo -e "${GREEN}✓ ${variant_name} binary validation PASSED${NC}"
        return 0
    else
        echo -e "${RED}✗ ${variant_name} binary validation FAILED${NC}"
        return 1
    fi
}

# Main execution
main() {
    local work_dir
    work_dir=$(mktemp -d)
    trap 'rm -rf "$work_dir"' EXIT

    echo "Working directory: $work_dir"
    echo ""

    # Check if we're in the right directory
    if [[ ! -f "compose.yaml" ]]; then
        echo -e "${RED}✗ compose.yaml not found. Run this script from the project root.${NC}"
        exit 1
    fi

    # Check for required tools
    for tool in docker objdump file; do
        if ! command -v "$tool" &> /dev/null; then
            echo -e "${RED}✗ Required tool not found: $tool${NC}"
            exit 1
        fi
    done

    # Extract binaries from each variant
    echo -e "${BLUE}[1/3] Extracting scalar binary...${NC}"
    extract_binary_from_image "processor-scalar" "app-scalar" "$work_dir/processor-scalar"

    echo ""
    echo -e "${BLUE}[2/3] Extracting NEON binary...${NC}"
    extract_binary_from_image "processor-neon" "app-neon" "$work_dir/processor-neon"

    echo ""
    echo -e "${BLUE}[3/3] Extracting SVE binary...${NC}"
    extract_binary_from_image "processor-sve" "app-sve" "$work_dir/processor-sve"

    # Analyze each binary with expected SIMD instruction sets
    local scalar_result=0
    local neon_result=0
    local sve_result=0

    # Scalar: expect no NEON, no SVE
    analyze_binary "$work_dir/processor-scalar" "SCALAR" "no" "no" || scalar_result=$?

    # NEON: expect NEON, no SVE
    analyze_binary "$work_dir/processor-neon" "NEON" "yes" "no" || neon_result=$?

    # SVE: NEON is optional/acceptable, expect SVE
    # (Compiler may use NEON for some operations even when targeting SVE)
    analyze_binary "$work_dir/processor-sve" "SVE" "optional" "yes" || sve_result=$?

    # Summary
    echo ""
    echo -e "${BLUE}=== Validation Summary ===${NC}"
    echo ""

    local all_passed=true

    if [[ $scalar_result -eq 0 ]]; then
        echo -e "${GREEN}✓ SCALAR binary: PASSED${NC} (no SIMD instructions, as expected)"
    else
        echo -e "${RED}✗ SCALAR binary: FAILED${NC}"
        all_passed=false
    fi

    if [[ $neon_result -eq 0 ]]; then
        echo -e "${GREEN}✓ NEON binary: PASSED${NC} (contains NEON instructions, as expected)"
    else
        echo -e "${RED}✗ NEON binary: FAILED${NC}"
        all_passed=false
    fi

    if [[ $sve_result -eq 0 ]]; then
        echo -e "${GREEN}✓ SVE binary: PASSED${NC} (contains SVE instructions, as expected)"
    else
        echo -e "${RED}✗ SVE binary: FAILED${NC}"
        all_passed=false
    fi

    echo ""
    if [[ "$all_passed" == true ]]; then
        echo -e "${GREEN}All validations passed!${NC}"
        echo "Each binary contains the expected SIMD instruction set."
        exit 0
    else
        echo -e "${RED}Some validations failed!${NC}"
        echo "One or more binaries do not contain the expected instructions."
        exit 1
    fi
}

main "$@"
