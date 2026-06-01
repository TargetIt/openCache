# GPGPU-Sim Cache Reference Implementation

This directory contains the **UNMODIFIED** GPGPU-Sim cache code (from
`gpgpu-sim_distribution/src/gpgpu-sim/gpu-cache.h` and `gpu-cache.cc`),
extracted for standalone compilation and testing.

## Quick Start (One-Click)

```bash
./run.sh
```

That's it. Builds and runs all 12 tests in one command.

## Manual Build

```bash
g++ -std=c++17 -Wall -Wno-unused -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_main.cc \
    -I gpgpu_cache \
    -o test_gpgpu_cache_ref

./test_gpgpu_cache_ref
```

Or with CMake:

```bash
mkdir build && cd build
cmake ..
make
./test_gpgpu_cache_ref
```

## Purpose

Provide a baseline reference to compare against openCache. The cache logic
(code in class bodies, function implementations) is **100% identical** to
GPGPU-Sim. Only `#include` directives were changed to point to stub headers.
The 3 `inc_aggregated_stats` function bodies were stubbed (they only do
hierarchical stats aggregation, zero cache logic impact).

## Files

| File | Origin | Modified? |
|------|--------|-----------|
| `gpgpu_cache/gpu-cache.h` | GPGPU-Sim `src/gpgpu-sim/gpu-cache.h` | **No** — original preserved |
| `gpgpu_cache/gpu-cache.cc` | GPGPU-Sim `src/gpgpu-sim/gpu-cache.cc` | **No** — original preserved |
| `gpgpu_cache/gpu_cache_ref.h` | Derived from `gpu-cache.h` | **Only `#include` lines changed** |
| `gpgpu_cache/gpu_cache_ref.cc` | Derived from `gpu-cache.cc` | **Only `#include` + 3 stub function bodies** |
| `gpgpu_cache/gpgpu_stubs.h` | Written for this reference | Provides GPGPU-Sim dependencies |
| `test/test_main.cc` | Written for this reference | 12 tests covering all cache types |
| `run.sh` | One-click build & test script | |

## Cache Types Provided

- `baseline_cache` — base class for all data caches
- `read_only_cache` — L1 instruction / constant cache
- `data_cache` — read/write cache with configurable policies
- `l1_cache` — L1 data cache (Fermi write-evict + write-back)
- `l2_cache` — L2 shared cache
- `tex_cache` — texture cache (FIFO pipeline, Igehy et al. 1998)
- `tag_array` — tag storage with LRU/FIFO replacement
- `mshr_table` — Miss Status Holding Register
- `cache_config` — GPGPU-Sim config string format
- `cache_stats` — statistics collection
