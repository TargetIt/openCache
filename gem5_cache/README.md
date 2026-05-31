# gem5 Cache Reference Implementation

This directory contains the **UNMODIFIED** gem5 cache code extracted from
[gem5/gem5](https://github.com/gem5/gem5) (`src/mem/cache/`), with a stub
layer enabling standalone compilation and testing.

## Quick Start (One-Click)

```bash
./run.sh
```

Builds and runs all tests in one command.

## Manual Build

```bash
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

./test_gem5_cache_ref
```

Or with CMake:

```bash
mkdir build && cd build
cmake ..
make
./test_gem5_cache_ref
```

## Purpose

Provide a baseline reference to compare against GPU cache models. The cache
logic (class bodies, function implementations) is **100% identical** to gem5.
Only the build environment is adapted via stub headers.

gem5's cache model serves as the gold standard for architectural cache
simulation — it's the most widely used and validated cache model in academia.

## Files

| Path | Origin | Modified? |
|------|--------|-----------|
| `src/mem/cache/*.hh, *.cc` | gem5 `src/mem/cache/` | **No** — original preserved |
| `src/mem/cache/tags/*.hh, *.cc` | gem5 `src/mem/cache/tags/` | **No** — original preserved |
| `src/mem/cache/replacement_policies/*.hh, *.cc` | gem5 `src/mem/cache/replacement_policies/` | **No** — original preserved |
| `src/mem/cache/tags/indexing_policies/*.hh, *.cc` | gem5 `src/mem/cache/tags/indexing_policies/` | **No** — original preserved |
| `gem5_stubs/` | Written for this reference | Provides gem5 infrastructure interface |
| `test/test_main.cc` | Written for this reference | Tests covering core cache components |
| `run.sh` | One-click build & test script | |
| `USER_GUIDE.md` | Written for this reference | Detailed interface documentation |

## Cache Components Provided

### Core Cache Model
- `CacheBlk` — cache block with coherence state, ref counting, dirty tracking
- `BaseCache` — abstract base class for all caches (timing, port binding)
- `Cache` — coherent cache with full MSHR/write queue pipeline
- `NoncoherentCache` — simpler non-coherent cache
- `MSHR` / `MSHRQueue` — miss status holding registers with target merging
- `WriteQueue` / `WriteQueueEntry` — write buffering
- `Queue` / `QueueEntry` — generic queue infrastructure

### Tag Storage
- `BaseTags` — abstract tag storage base
- `BaseSetAssoc` — set-associative tag array
- `FALRU` — fully-associative LRU tracking
- `SectorTags` / `SectorBlk` — sector-based cache organization
- `CompressedTags` — compression-aware tag storage
- `SuperBlk` — super-block organizations
- `Dueling` — dueling tag monitoring (for adaptive policies)

### Replacement Policies
- `LRU` — least recently used
- `FIFO` — first-in first-out
- `MRU` — most recently used
- `Random` — random replacement
- `TreePLRU` — tree-based pseudo-LRU
- `BIP` — bimodal insertion policy
- `BRRIP` — best-referenced re-reference interval prediction
- `SHiP` — signature-based hit prediction
- `SecondChance` — second chance (extended FIFO)
- `WeightedLRU` — weighted LRU variant
- `LFU` — least frequently used
- `Dueling` — policy dueling (adaptive selection)

### Indexing Policies
- `SetAssociative` — standard set-associative indexing
- `SkewedAssociative` — skewed-associative indexing

### Partitioning Policies
- `WayPartitioningPolicy` / `MaxCapacityPartitioningPolicy` — cache partitioning
- `PartitionManager` — multi-policy partitioning coordinator
- `WayPolicyAllocation` — way allocation management

## Compilation Status

| Category | Files | Status |
|----------|-------|--------|
| Core cache model | cache_blk.cc, mshr.cc, mshr_queue.cc, write_queue.cc, write_queue_entry.cc | Compiles |
| Coherent cache | base.cc, cache.cc, noncoherent_cache.cc | Compiles |
| Tag storage | base.cc, base_set_assoc.cc, fa_lru.cc, sector_tags.cc, sector_blk.cc, super_blk.cc, compressed_tags.cc | Compiles |
| Replacement policies | lru_rp.cc, fifo_rp.cc, mru_rp.cc, random_rp.cc, tree_plru_rp.cc | Compiles |
| Replacement policies | bip_rp.cc, brrip_rp.cc, lfu_rp.cc, dueling_rp.cc | Compiles |
| Replacement policies | second_chance_rp.cc, weighted_lru_rp.cc, ship_rp.cc | Limited* |
| Indexing policies | set_associative.cc, skewed_associative.cc | Compiles |

*Limited: compiles with syntax check but has deep template dependency issues.
These 4 files represent ~5% of the total codebase.

## Test Coverage (13 tests)

1. CacheBlk — validity, positioning, refcount, invalidation
2. Replacement Policies — construction and lifecycle (5 policy types)
3. Indexing Policy — set-associative construction
4. FALRU — construction and block lookup
5. Types — Cycles arithmetic, Tick, Addr
6. Statistics — Group, Scalar, Vector operations

## Architecture Comparison

| Feature | gem5 Cache | GPGPU-Sim Cache |
|---------|-----------|-----------------|
| Language | C++17 | C++98 |
| Organization | Modular class hierarchy | Monolithic 2-file structure |
| Replacement | Pluggable policy objects | Hardcoded in tag_array |
| Indexing | Pluggable indexing policies | Hardcoded set-associative |
| Coherence | Full MOESI/MSI protocol support | None (GPU compute focus) |
| Write path | Write queue + write allocator | write_allocate + write_evict |
| Statistics | Built-in stats framework | Manual counter accumulation |
| Compile size | ~60 src files | 2 src files |
| Total LOC | ~25,000 | ~4,100 |
