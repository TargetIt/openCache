#!/bin/bash
set -e
cd "$(dirname "$0")"

echo "============================================"
echo " GPGPU-Sim Cache Reference — Build & Test"
echo "============================================"

echo ""
echo "[1/5] Building unit tests..."
g++ -std=c++17 -Wall -Wno-unused -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_main.cc \
    -I gpgpu_cache \
    -o test_gpgpu_cache_ref

echo "[2/5] Building scenario tests..."
g++ -std=c++17 -Wall -Wno-unused -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_scenario.cc \
    -I gpgpu_cache \
    -o test_scenario

echo "[3/5] Building deep whitebox tests..."
g++ -std=c++17 -Wall -Wno-unused -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_cache_whitebox.cc \
    -I gpgpu_cache \
    -o test_cache_whitebox

echo "[4/5] Building death tests..."
g++ -std=c++17 -Wall -Wno-unused -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_death.cc \
    -I gpgpu_cache \
    -o test_death

echo ""
echo "[5/5] Running all tests..."
echo ""
echo "--- Unit Tests ---"
./test_gpgpu_cache_ref

echo ""
echo "--- Scenario Integration Tests ---"
./test_scenario

echo ""
echo "--- Deep Whitebox Tests ---"
./test_cache_whitebox

echo ""
echo "--- Death Tests ---"
./test_death

echo ""
echo "Done."
