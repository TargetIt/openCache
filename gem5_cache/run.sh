#!/bin/bash
set -e
cd "$(dirname "$0")"

echo "============================================"
echo " gem5 Cache Reference -- Build & Test"
echo "============================================"

echo ""
echo "[1/2] Building test executable..."
g++ -std=c++17 -Wall -Wno-unused -Wno-non-pod-varargs \
    -Wno-unused-parameter -Wno-unused-variable \
    -g -O0 \
    gem5_stubs/stubs.cc \
    src/mem/cache/cache_blk.cc \
    src/mem/cache/tags/base.cc \
    src/mem/cache/tags/base_set_assoc.cc \
    src/mem/cache/tags/fa_lru.cc \
    src/mem/cache/tags/indexing_policies/set_associative.cc \
    src/mem/cache/replacement_policies/lru_rp.cc \
    src/mem/cache/replacement_policies/fifo_rp.cc \
    src/mem/cache/replacement_policies/mru_rp.cc \
    src/mem/cache/replacement_policies/random_rp.cc \
    src/mem/cache/replacement_policies/tree_plru_rp.cc \
    test/test_main.cc \
    -I gem5_stubs \
    -I src \
    -o test_gem5_cache_ref

echo ""
echo "[2/2] Running all tests..."
echo ""

./test_gem5_cache_ref

echo ""
echo "Done."
