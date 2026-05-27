#!/bin/bash
set -e
cd "$(dirname "$0")"

echo "============================================"
echo " GPGPU-Sim Cache Reference — Build & Test"
echo "============================================"

echo ""
echo "[1/3] Building unit tests..."
g++ -std=c++17 -Wall -Wno-unused -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_main.cc \
    -I gpgpu_cache \
    -o test_gpgpu_cache_ref

echo "[2/3] Building scenario tests..."
g++ -std=c++17 -Wall -Wno-unused -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_scenario.cc \
    -I gpgpu_cache \
    -o test_scenario

echo ""
echo "[3/3] Running all tests..."
echo ""
echo "--- Unit Tests ---"
./test_gpgpu_cache_ref

echo ""
echo "--- Scenario Integration Tests ---"
./test_scenario

echo ""
echo "Done."
