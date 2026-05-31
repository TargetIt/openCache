#include "scenarios.h"

namespace opencache {
namespace scenario {

// ===== GPU L1 Data Cache =====
// Typical NVIDIA-style L1: 128KB unified (L1 + shared memory)
// Sector cache, write-evict for global + write-back for local
// 4 banks, 32 sets, 4 ways, 128B line, LRU
std::unique_ptr<DataCache> create_l1_data_cache(CacheMemoryInterface *lower_mem) {
    CacheConfig config;
    config.cache_type = CacheType::SECTOR;
    config.num_sets = 32;
    config.line_size = 128;
    config.associativity = 4;
    config.num_banks = 4;
    config.replacement_policy = ReplacementPolicy::LRU;
    config.write_policy = WritePolicy::WRITE_EVICT; // global write-evict
    config.alloc_policy = AllocationPolicy::ON_MISS;
    config.write_alloc_policy = WriteAllocatePolicy::LAZY_FETCH_ON_READ;
    config.set_index_func = SetIndexFunction::BITWISE_XOR;
    config.mshr_entries = 32;
    config.mshr_max_merge = 4;
    config.miss_queue_size = 64;
    config.hit_latency = 1;
    config.fill_latency = 30;
    config.compute_derived();

    return std::unique_ptr<DataCache>(
        new DataCache("L1_Data_Cache", config, 0, 0, lower_mem, CacheLevel::L1));
}

// ===== GPU L2 Cache =====
// Shared L2: large, write-back, 16-way, multiple banks
std::unique_ptr<DataCache> create_l2_cache(CacheMemoryInterface *lower_mem) {
    CacheConfig config;
    config.cache_type = CacheType::NORMAL;
    config.num_sets = 256;
    config.line_size = 128;
    config.associativity = 16;
    config.num_banks = 8;
    config.replacement_policy = ReplacementPolicy::LRU;
    config.write_policy = WritePolicy::WRITE_BACK;
    config.alloc_policy = AllocationPolicy::ON_MISS;
    config.write_alloc_policy = WriteAllocatePolicy::FETCH_ON_WRITE;
    config.set_index_func = SetIndexFunction::BITWISE_XOR;
    config.mshr_entries = 64;
    config.mshr_max_merge = 8;
    config.miss_queue_size = 128;
    config.hit_latency = 10;
    config.fill_latency = 100;
    config.compute_derived();

    return std::unique_ptr<DataCache>(
        new DataCache("L2_Cache", config, 0, 1, lower_mem, CacheLevel::L2));
}

// ===== Read-Only Cache =====
// For constant cache / instruction cache
std::unique_ptr<ReadOnlyCache> create_read_only_cache(CacheMemoryInterface *lower_mem) {
    CacheConfig config;
    config.cache_type = CacheType::NORMAL;
    config.num_sets = 16;
    config.line_size = 64;
    config.associativity = 4;
    config.num_banks = 1;
    config.replacement_policy = ReplacementPolicy::LRU;
    config.write_policy = WritePolicy::READ_ONLY;
    config.alloc_policy = AllocationPolicy::ON_MISS;
    config.set_index_func = SetIndexFunction::LINEAR;
    config.mshr_entries = 8;
    config.mshr_max_merge = 2;
    config.miss_queue_size = 16;
    config.hit_latency = 1;
    config.fill_latency = 40;
    config.compute_derived();

    return std::unique_ptr<ReadOnlyCache>(
        new ReadOnlyCache("ReadOnly_Cache", config, 0, 2, lower_mem, CacheLevel::L1));
}

// ===== Write-Through Cache =====
std::unique_ptr<DataCache> create_write_through_cache(CacheMemoryInterface *lower_mem) {
    CacheConfig config;
    config.cache_type = CacheType::NORMAL;
    config.num_sets = 64;
    config.line_size = 64;
    config.associativity = 8;
    config.num_banks = 1;
    config.replacement_policy = ReplacementPolicy::LRU;
    config.write_policy = WritePolicy::WRITE_THROUGH;
    config.alloc_policy = AllocationPolicy::ON_MISS;
    config.write_alloc_policy = WriteAllocatePolicy::NO_WRITE_ALLOCATE;
    config.set_index_func = SetIndexFunction::LINEAR;
    config.mshr_entries = 16;
    config.mshr_max_merge = 4;
    config.miss_queue_size = 32;
    config.hit_latency = 2;
    config.fill_latency = 20;
    config.compute_derived();

    return std::unique_ptr<DataCache>(
        new DataCache("WriteThrough_Cache", config, 0, 3, lower_mem, CacheLevel::L1));
}

// ===== Write-Back Cache =====
std::unique_ptr<DataCache> create_write_back_cache(CacheMemoryInterface *lower_mem) {
    CacheConfig config;
    config.cache_type = CacheType::NORMAL;
    config.num_sets = 128;
    config.line_size = 64;
    config.associativity = 8;
    config.num_banks = 2;
    config.replacement_policy = ReplacementPolicy::LRU;
    config.write_policy = WritePolicy::WRITE_BACK;
    config.alloc_policy = AllocationPolicy::ON_MISS;
    config.write_alloc_policy = WriteAllocatePolicy::FETCH_ON_WRITE;
    config.set_index_func = SetIndexFunction::BITWISE_XOR;
    config.mshr_entries = 32;
    config.mshr_max_merge = 8;
    config.miss_queue_size = 64;
    config.hit_latency = 3;
    config.fill_latency = 50;
    config.compute_derived();

    return std::unique_ptr<DataCache>(
        new DataCache("WriteBack_Cache", config, 0, 4, lower_mem, CacheLevel::L2));
}

// ===== Write-Evict Cache =====
std::unique_ptr<DataCache> create_write_evict_cache(CacheMemoryInterface *lower_mem) {
    CacheConfig config;
    config.cache_type = CacheType::NORMAL;
    config.num_sets = 64;
    config.line_size = 128;
    config.associativity = 4;
    config.num_banks = 4;
    config.replacement_policy = ReplacementPolicy::LRU;
    config.write_policy = WritePolicy::WRITE_EVICT;
    config.alloc_policy = AllocationPolicy::ON_MISS;
    config.write_alloc_policy = WriteAllocatePolicy::NO_WRITE_ALLOCATE;
    config.set_index_func = SetIndexFunction::LINEAR;
    config.mshr_entries = 16;
    config.mshr_max_merge = 4;
    config.miss_queue_size = 32;
    config.hit_latency = 1;
    config.fill_latency = 40;
    config.compute_derived();

    return std::unique_ptr<DataCache>(
        new DataCache("WriteEvict_Cache", config, 0, 5, lower_mem, CacheLevel::L1));
}

// ===== Texture Cache =====
// Read-only, sector cache for spatial locality in texture accesses
std::unique_ptr<ReadOnlyCache> create_texture_cache(CacheMemoryInterface *lower_mem) {
    CacheConfig config;
    config.cache_type = CacheType::SECTOR;
    config.num_sets = 64;
    config.line_size = 128;
    config.associativity = 4;
    config.num_banks = 2;
    config.replacement_policy = ReplacementPolicy::LRU;
    config.write_policy = WritePolicy::READ_ONLY;
    config.alloc_policy = AllocationPolicy::ON_MISS;
    config.set_index_func = SetIndexFunction::HASH_IPOLY;
    config.mshr_entries = 24;
    config.mshr_max_merge = 4;
    config.miss_queue_size = 48;
    config.hit_latency = 5;
    config.fill_latency = 60;
    config.compute_derived();

    return std::unique_ptr<ReadOnlyCache>(
        new ReadOnlyCache("Texture_Cache", config, 0, 6, lower_mem, CacheLevel::L1));
}

// ===== Streaming Cache =====
// ON_FILL allocation, useful for streaming workloads like memcpy
std::unique_ptr<DataCache> create_streaming_cache(CacheMemoryInterface *lower_mem) {
    CacheConfig config;
    config.cache_type = CacheType::NORMAL;
    config.num_sets = 16;
    config.line_size = 128;
    config.associativity = 4;
    config.num_banks = 1;
    config.replacement_policy = ReplacementPolicy::FIFO;
    config.write_policy = WritePolicy::WRITE_EVICT; // streaming: evict after write
    config.alloc_policy = AllocationPolicy::ON_FILL;
    config.write_alloc_policy = WriteAllocatePolicy::NO_WRITE_ALLOCATE;
    config.set_index_func = SetIndexFunction::LINEAR;
    config.mshr_entries = 128; // many MSHR for throughput
    config.mshr_max_merge = 1;
    config.miss_queue_size = 256;
    config.hit_latency = 1;
    config.fill_latency = 100;
    config.compute_derived();

    return std::unique_ptr<DataCache>(
        new DataCache("Streaming_Cache", config, 0, 7, lower_mem, CacheLevel::L1));
}

// ===== Parameterizable factories =====

std::unique_ptr<DataCache> create_data_cache(CacheMemoryInterface *lower_mem,
                                              const CacheConfig &config,
                                              const std::string &name) {
    CacheConfig cfg = config;
    cfg.compute_derived();

    return std::unique_ptr<DataCache>(
        new DataCache(name, cfg, 0, 0, lower_mem, CacheLevel::OTHER));
}

std::unique_ptr<ReadOnlyCache> create_read_only_cache(CacheMemoryInterface *lower_mem,
                                                       const CacheConfig &config,
                                                       const std::string &name) {
    CacheConfig cfg = config;
    cfg.write_policy = WritePolicy::READ_ONLY;
    cfg.compute_derived();

    return std::unique_ptr<ReadOnlyCache>(
        new ReadOnlyCache(name, cfg, 0, 0, lower_mem, CacheLevel::OTHER));
}

} // namespace scenario
} // namespace opencache
