#ifndef OPEN_CACHE_SCENARIOS_H
#define OPEN_CACHE_SCENARIOS_H

#include "../src/open_cache.h"
#include <memory>

namespace opencache {
namespace scenario {

// Factory functions for different cache scenarios.
// Each returns a fully configured cache instance ready for use.

// GPU L1 Data Cache — typically sector cache, write-through or write-evict + writeback
std::unique_ptr<DataCache> create_l1_data_cache(CacheMemoryInterface *lower_mem);

// GPU L2 Cache — large shared cache, write-back, write-allocate
std::unique_ptr<DataCache> create_l2_cache(CacheMemoryInterface *lower_mem);

// Read-Only Cache — for constant/instruction caches
std::unique_ptr<ReadOnlyCache> create_read_only_cache(CacheMemoryInterface *lower_mem);

// Write-Through Cache — all writes propagate to lower level
std::unique_ptr<DataCache> create_write_through_cache(CacheMemoryInterface *lower_mem);

// Write-Back Cache — writes only go to lower level on eviction
std::unique_ptr<DataCache> create_write_back_cache(CacheMemoryInterface *lower_mem);

// Write-Evict Cache — writes evict the cache line
std::unique_ptr<DataCache> create_write_evict_cache(CacheMemoryInterface *lower_mem);

// Texture Cache — read-only with sector support for spatial locality
std::unique_ptr<ReadOnlyCache> create_texture_cache(CacheMemoryInterface *lower_mem);

// Stream Cache — for streaming workloads, use ON_FILL allocation
std::unique_ptr<DataCache> create_streaming_cache(CacheMemoryInterface *lower_mem);

// Fully parameterized factory — create any cache from explicit config
std::unique_ptr<DataCache> create_data_cache(CacheMemoryInterface *lower_mem,
                                              const CacheConfig &config,
                                              const std::string &name);

std::unique_ptr<ReadOnlyCache> create_read_only_cache(CacheMemoryInterface *lower_mem,
                                                       const CacheConfig &config,
                                                       const std::string &name);

} // namespace scenario
} // namespace opencache

#endif // OPEN_CACHE_SCENARIOS_H
