#!/bin/bash
set -e
cd "$(dirname "$0")"

OUT_DIR="${1:-coverage}"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

LLVM_PROFDATA="$(command -v llvm-profdata || xcrun --find llvm-profdata 2>/dev/null || true)"
LLVM_COV="$(command -v llvm-cov || xcrun --find llvm-cov 2>/dev/null || true)"

if [ -n "$LLVM_PROFDATA" ] && [ -n "$LLVM_COV" ]; then
    CXX="${CXX:-clang++}"
    if ! command -v "$CXX" >/dev/null 2>&1; then
        echo "coverage: clang++ not found; cannot run llvm coverage"
        exit 2
    fi

    CXXFLAGS="-std=c++17 -Wall -Wno-unused -g -O0 -fprofile-instr-generate -fcoverage-mapping"

    "$CXX" $CXXFLAGS gpgpu_cache/gpu_cache_ref.cc test/test_main.cc \
        -I gpgpu_cache -o "$OUT_DIR/test_gpgpu_cache_ref_cov"
    "$CXX" $CXXFLAGS gpgpu_cache/gpu_cache_ref.cc test/test_scenario.cc \
        -I gpgpu_cache -o "$OUT_DIR/test_scenario_cov"
    "$CXX" $CXXFLAGS gpgpu_cache/gpu_cache_ref.cc test/test_cache_whitebox.cc \
        -I gpgpu_cache -o "$OUT_DIR/test_cache_whitebox_cov"

    LLVM_PROFILE_FILE="$OUT_DIR/unit.profraw" "$OUT_DIR/test_gpgpu_cache_ref_cov"
    LLVM_PROFILE_FILE="$OUT_DIR/scenario.profraw" "$OUT_DIR/test_scenario_cov"
    LLVM_PROFILE_FILE="$OUT_DIR/whitebox.profraw" "$OUT_DIR/test_cache_whitebox_cov"

    "$LLVM_PROFDATA" merge -sparse "$OUT_DIR"/*.profraw -o "$OUT_DIR/coverage.profdata"
    "$LLVM_COV" report \
        "$OUT_DIR/test_gpgpu_cache_ref_cov" \
        -object "$OUT_DIR/test_scenario_cov" \
        -object "$OUT_DIR/test_cache_whitebox_cov" \
        -instr-profile "$OUT_DIR/coverage.profdata" \
        gpgpu_cache/gpu_cache_ref.cc gpgpu_cache/gpu_cache_ref.h \
        | tee "$OUT_DIR/coverage-summary.txt"
    exit 0
fi

echo "coverage: no supported coverage tool found (llvm-profdata/llvm-cov)"
exit 2
