# GPGPU-Sim Cache Reference Implementation

This directory contains the **UNMODIFIED** GPGPU-Sim cache code (from
`gpgpu-sim_distribution/src/gpgpu-sim/gpu-cache.h` and `gpu-cache.cc`),
extracted for standalone compilation and testing.

## Quick Start (One-Click)

```bash
./run.sh
```

That's it. Builds and runs all 13 tests in one command.

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

### Compiled (standalone reference build)

| File | Origin | Modified? |
|------|--------|-----------|
| `gpgpu_cache/gpu_cache_ref.h` | Derived from `gpu-cache.h` | **Only `#include` lines changed** |
| `gpgpu_cache/gpu_cache_ref.cc` | Derived from `gpu-cache.cc` | **Only `#include` + 3 stub function bodies** |
| `gpgpu_cache/gpgpu_stubs.h` | Written for this reference | Provides GPGPU-Sim dependencies |
| `gpgpu_cache/data_store.h` | GPGPU-Sim `data_store.h` | **No** — original preserved |
| `gpgpu_cache/memory_system.h` | GPGPU-Sim `memory_system.h` | **No** — original preserved |
| `test/test_main.cc` | Written for this reference | 13 tests covering all cache types |
| `test/test_cache_whitebox.cc` | Written for this reference | 116 deep whitebox tests |
| `test/test_scenario.cc` | Written for this reference | 10 scenario integration tests |
| `test/test_death.cc` | Written for this reference | 16 config parsing death tests |
| `run.sh` | One-click build & test script | |
| `CMakeLists.txt` | CMake build config | |
| `coverage.sh` | Coverage report script | |

### Preserved originals (`../reference-only-originals-gpgpusim/`) — not compiled

These are the **UNMODIFIED** GPGPU-Sim source files, kept solely as a correctness baseline.
They are never compiled into the standalone reference build.

| File | Origin |
|------|--------|
| `gpu-cache.h` | GPGPU-Sim `src/gpgpu-sim/gpu-cache.h` |
| `gpu-cache.cc` | GPGPU-Sim `src/gpgpu-sim/gpu-cache.cc` |
| `gpu-misc.h` / `gpu-misc.cc` | GPGPU-Sim `src/gpgpu-sim/gpu-misc.*` |
| `hashing.h` / `hashing.cc` | GPGPU-Sim hashing utilities |
| `addrdec.h` / `addrdec.cc` | GPGPU-Sim address decoder |
| `mem_fetch.h` / `mem_fetch_status.tup` | GPGPU-Sim memory fetch types |
| `abstract_hardware_model.h` | GPGPU-Sim hardware model types |
| `delayqueue.h` | GPGPU-Sim delay queue |

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
