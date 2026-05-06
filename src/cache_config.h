#ifndef OPEN_CACHE_CONFIG_H
#define OPEN_CACHE_CONFIG_H

#include "open_cache_types.h"
#include <string>
#include <cstdio>
#include <cassert>
#include <cmath>

namespace opencache {

inline constexpr uint32_t log2_u32(uint32_t x) {
    assert(x > 0 && (x & (x - 1)) == 0); // must be power of 2
    uint32_t r = 0;
    while (x >>= 1) r++;
    return r;
}

class CacheConfig {
public:
    // ===== Physical parameters =====
    uint32_t num_sets = 64;         // number of sets
    uint32_t line_size = 128;       // cache line size in bytes
    uint32_t associativity = 4;     // number of ways per set
    uint32_t num_banks = 1;         // number of banks
    uint32_t sector_size = DEFAULT_SECTOR_SIZE;

    // ===== Timing parameters =====
    uint32_t hit_latency = 1;       // cycles for a hit
    uint32_t fill_latency = 10;     // cycles for a fill from lower level
    uint32_t data_port_width = 0;   // 0 = same as line_size
    uint32_t data_port_latency = 1; // cycles data port is busy
    uint32_t fill_port_latency = 1; // cycles fill port is busy

    // ===== MSHR parameters =====
    uint32_t mshr_entries = 16;
    uint32_t mshr_max_merge = 4;
    uint32_t miss_queue_size = 32;

    // ===== Policy parameters =====
    CacheType cache_type = CacheType::NORMAL;
    ReplacementPolicy replacement_policy = ReplacementPolicy::LRU;
    WritePolicy write_policy = WritePolicy::WRITE_BACK;
    AllocationPolicy alloc_policy = AllocationPolicy::ON_MISS;
    WriteAllocatePolicy write_alloc_policy = WriteAllocatePolicy::FETCH_ON_WRITE;
    SetIndexFunction set_index_func = SetIndexFunction::LINEAR;
    MSHRType mshr_type = MSHRType::ASSOC;

    // ===== Read/write ratio (for L1 streaming) =====
    uint32_t write_percent = 0;     // 0 = equal, otherwise % dedicated to writes

    // ===== Derived parameters (computed after config is set) =====
    uint32_t line_size_log2 = 0;
    uint32_t num_sets_log2 = 0;
    uint32_t atom_size = 0;         // cache_type==SECTOR ? sector_size : line_size
    uint32_t sector_chunk_size = DEFAULT_SECTOR_CHUNK_SIZE;

    CacheConfig() { compute_derived(); }

    // Quick constructor for common use cases
    CacheConfig(uint32_t nsets, uint32_t lsize, uint32_t assoc,
                ReplacementPolicy rp = ReplacementPolicy::LRU,
                WritePolicy wp = WritePolicy::WRITE_BACK)
        : num_sets(nsets), line_size(lsize), associativity(assoc),
          replacement_policy(rp), write_policy(wp)
    { compute_derived(); }

    void compute_derived() {
        line_size_log2 = log2_u32(line_size);
        num_sets_log2 = log2_u32(num_sets);

        if (cache_type == CacheType::SECTOR) {
            atom_size = sector_size;
            sector_chunk_size = line_size / sector_size;
            assert(line_size % sector_size == 0 &&
                   "Sector cache: line_size must be multiple of sector_size");
        } else {
            atom_size = line_size;
            sector_chunk_size = 1;
        }

        // Auto-adjust data_port_width if not explicitly set or incompatible with new line_size
        if (data_port_width == 0 || (line_size % data_port_width != 0)) {
            data_port_width = line_size;
        }
        assert(line_size % data_port_width == 0 &&
               "line_size must be multiple of data_port_width");
    }

    uint32_t get_num_lines() const { return num_sets * associativity; }

    uint32_t get_total_size_bytes() const {
        return num_sets * associativity * line_size;
    }

    uint32_t get_total_size_kb() const {
        return get_total_size_bytes() / 1024;
    }

    // Extract tag from address (block-aligned)
    addr_t get_tag(addr_t addr) const {
        return addr & ~(static_cast<addr_t>(line_size) - 1);
    }

    // Extract block address
    addr_t get_block_addr(addr_t addr) const {
        return addr & ~(static_cast<addr_t>(line_size) - 1);
    }

    // MSHR granularity address
    addr_t get_mshr_addr(addr_t addr) const {
        return addr & ~(static_cast<addr_t>(atom_size) - 1);
    }

    // Compute set index from address
    uint32_t get_set_index(addr_t addr) const;

    // Compute bank index from address
    uint32_t get_bank_index(addr_t addr) const;

    // Compute sector mask from address
    sector_mask_t get_sector_mask(addr_t addr) const {
        sector_mask_t mask;
        if (cache_type == CacheType::SECTOR) {
            uint32_t sector_idx = (addr & (line_size - 1)) / sector_size;
            mask.set(sector_idx);
        } else {
            mask.set(); // all sectors for line cache (non-sector)
        }
        return mask;
    }

    // Hash function for set index
    uint32_t hash_addr(addr_t addr, uint32_t nset, uint32_t lsize_log2,
                       uint32_t nset_log2, SetIndexFunction func) const;

    // Check if this is a streaming cache
    bool is_streaming() const { return alloc_policy == AllocationPolicy::ON_FILL; }

    // Build a config string (GPGPU-Sim compatible format)
    std::string to_config_string() const;

    // Parse from a config string (GPGPU-Sim compatible format)
    // Format: <S|N>:<nsets>:<bsize>:<assoc>,<L|F>:<R|B|T|E>:<m|f>:<N|W|F|L>:<L|X|H|P>:<A|S|F|T>,<mshr>:<merge>,<mq>
    // Or simplified: <nsets>:<bsize>:<assoc>
    bool parse_config_string(const char *config_str);

    void print(FILE *fp = stdout) const {
        fprintf(fp, "Cache: %d KB (%d sets x %d ways x %d B line), "
                "banks=%d, MSHR=%d, hit_latency=%d, fill_latency=%d\n",
                get_total_size_kb(), num_sets, associativity, line_size,
                num_banks, mshr_entries, hit_latency, fill_latency);
    }

private:
    void exit_parse_error(const char *config_str) const {
        fprintf(stderr, "openCache: cache configuration parsing error (%s)\n",
                config_str);
    }
};

} // namespace opencache

#endif // OPEN_CACHE_CONFIG_H
