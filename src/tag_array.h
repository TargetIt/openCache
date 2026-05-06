#ifndef OPEN_CACHE_TAG_ARRAY_H
#define OPEN_CACHE_TAG_ARRAY_H

#include "open_cache_types.h"
#include "cache_config.h"
#include "cache_block.h"
#include <unordered_map>
#include <vector>
#include <cstdio>

namespace opencache {

struct TagProbeResult {
    AccessStatus status;
    uint32_t set_index;
    uint32_t way_index;
    uint32_t sector_index;     // for sector cache
    sector_mask_t sector_mask;
};

class TagArray {
public:
    TagArray(CacheConfig &config, int core_id = 0, int type_id = 0);
    ~TagArray();

    // Probe (read-only lookup) — for read-only caches
    TagProbeResult probe(addr_t addr, bool is_write, bool probe_mode = false) const;
    TagProbeResult probe(addr_t addr, sector_mask_t mask, bool is_write,
                         bool probe_mode = false) const;

    // Access (allocates on miss) — for data caches
    TagProbeResult access(addr_t addr, uint64_t time, bool is_write);
    TagProbeResult access(addr_t addr, uint64_t time, bool is_write,
                          bool &wb, EvictedBlockInfo &evicted);

    // Fill a previously allocated entry
    void fill(addr_t addr, uint64_t time, bool is_write);
    void fill(uint32_t idx, uint64_t time, bool is_write);
    void fill(addr_t addr, uint64_t time, sector_mask_t mask,
              byte_mask_t byte_mask, bool is_write);

    uint32_t size() const { return m_config.get_num_lines(); }
    CacheBlock *get_block(uint32_t idx) { return m_lines[idx]; }
    const CacheBlock *get_block(uint32_t idx) const { return m_lines[idx]; }

    void flush();
    void invalidate();

    void get_stats(uint32_t &total_access, uint32_t &total_misses,
                   uint32_t &total_hit_res, uint32_t &total_res_fail) const;

    void print(FILE *stream = stdout) const;

    // Replacement policy - find victim way in a set
    uint32_t find_victim(uint32_t set_index) const;
    // Update replacement state on access/fill
    void update_replacement_state(uint32_t set_index, uint32_t way_index, bool is_fill);

private:
    CacheConfig &m_config;
    std::vector<CacheBlock *> m_lines;  // flat array [num_sets * associativity]
    uint32_t m_num_lines;

    // Statistics
    uint32_t m_accesses = 0;
    uint32_t m_misses = 0;
    uint32_t m_pending_hits = 0;
    uint32_t m_res_fails = 0;
    uint32_t m_sector_misses = 0;
    uint32_t m_dirty = 0;

    // LRU state: for each set, track access ordering (front=MRU, back=LRU)
    std::vector<std::vector<uint32_t>> m_lru_order; // [set][way_pos] -> way_index

    // FIFO state: next way to evict for each set
    std::vector<uint32_t> m_fifo_next;

    int m_core_id;
    int m_type_id;

    // Get the flat index for a (set, way) pair
    uint32_t get_line_index(uint32_t set_index, uint32_t way_index) const {
        return set_index * m_config.associativity + way_index;
    }

    // Find matching way in a set
    int32_t find_matching_way(uint32_t set_index, addr_t tag) const;

    // Find an available (INVALID) way in a set
    int32_t find_invalid_way(uint32_t set_index) const;

    // Update LRU: promote way to MRU position
    void lru_promote(uint32_t set_index, uint32_t way_index);

    // Update FIFO: advance pointer
    void fifo_advance(uint32_t set_index);
};

} // namespace opencache

#endif // OPEN_CACHE_TAG_ARRAY_H
