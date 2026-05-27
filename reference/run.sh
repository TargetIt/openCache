#!/bin/bash
set -e
cd "$(dirname "$0")"

echo "============================================"
echo " GPGPU-Sim Cache Reference — Build & Test"
echo "============================================"

echo ""
echo "[1/2] Building..."
g++ -std=c++17 -Wall -Wno-unused -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_main.cc \
    -I gpgpu_cache \
    -o test_gpgpu_cache_ref

echo "[2/2] Running tests..."
echo ""
./test_gpgpu_cache_ref

echo ""
echo "Done."
