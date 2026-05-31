// =============================================================================
// MemorySystem — GPGPU-Sim Dual-Model Integration Example
//
// This file demonstrates how to wire GPGPU-Sim's separate concerns:
//
//   FUNCTIONAL MODEL (DataStore)         TIMING MODEL (Cache)
//   ──────────────────────────           ─────────────────────
//   Stores actual data bytes             Tracks addresses + states + delays
//   DataStore::read() / write()          tag_array::probe() / access() / fill()
//
// In the real GPGPU-Sim, the functional model is gpgpu_t::m_global_mem
// (a memory_space object).  The timing model is the cache hierarchy
// (l1_cache, l2_cache, interconnect, DRAM queues).  mem_fetch is a
// request token that flows through the timing pipeline — it never carries
// data payload.  Data transfer between levels is handled separately by
// copying between DataStores.
//
// Usage (GPGPU-Sim pattern):
//
//   // 1. Create separate DataStores for each memory level
//   DataStore l1_data, l2_data, dram_data;
//
//   // 2. Pre-populate DRAM with initial data (like loading a binary)
//   uint8_t init_data[128] = {...};
//   dram_data.write(0x1000, init_data, 128);
//
//   // 3. Create caches (timing model only — no data inside)
//   l1_cache l1(...);   // tag + state + MSHR + bandwidth
//   l2_cache l2(...);
//
//   // 4. On cache MISS → fill:
//   //    a) Copy data from lower DataStore to upper DataStore
//   //    b) Call cache.fill() to update tag state
//   if (l1.access() == MISS) {
//       auto data = l2_data.read(block_addr, line_sz);
//       l1_data.write(block_addr, data.data(), line_sz);  // FUNCTIONAL
//       l1.fill(&mf, time);                                 // TIMING
//   }
//
//   // 5. On cache HIT:
//   //    Read data from this level's DataStore
//   auto data = l1_data.read(block_addr, request_size);
//
//   // 6. On eviction / writeback:
//   //    Copy dirty data from upper DataStore to lower DataStore
//   auto dirty = l1_data.read(evicted_addr, modified_size);
//   l2_data.write(evicted_addr, dirty.data(), modified_size);
// =============================================================================

#ifndef MEMORY_SYSTEM_H
#define MEMORY_SYSTEM_H

#include "data_store.h"
#include "gpu_cache_ref.h"
#include <cstdio>
#include <cassert>
#include <cstring>

// =============================================================================
// SimpleTwoLevel — minimal L1 + DRAM system demonstrating dual-model separation
//
// This is a teaching example, not a production wrapper.  It shows the key
// insight: cache (timing) and DataStore (functional) are separate objects.
// =============================================================================
class SimpleTwoLevel {
public:
    // ---- Functional model (data) ----
    DataStore l1_data;
    DataStore dram_data;

    // ---- Timing model (cache) ----
    simple_mem_interface dram_if;
    simple_mf_allocator   allocator;
    gpgpu_sim              gpu;

private:
    cache_config    m_cfg;
    read_only_cache *m_cache;

public:
    SimpleTwoLevel(const char* config_str)
        : dram_if(256), m_cache(nullptr)
    {
        char* cfg_copy = strdup(config_str);
        m_cfg.m_config_string = cfg_copy;
        m_cfg.init(cfg_copy, FuncCachePreferNone);
        free(cfg_copy);

        m_cache = new read_only_cache("L1", m_cfg, 0, 0, &dram_if,
                                      IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    }

    ~SimpleTwoLevel() { delete m_cache; }

    /// Read `size` bytes at `addr` through the L1 cache.
    /// Returns a pair of (hit: bool, data: vector<uint8_t>).
    /// Call this ONCE per cycle with at most one request per cycle.
    std::pair<bool, std::vector<uint8_t>> read(new_addr_type addr, unsigned size,
                                                unsigned cycle) {
        // Block address for DataStore lookups
        new_addr_type block_addr = addr & ~(new_addr_type)(m_cfg.get_line_sz() - 1);
        unsigned offset = addr - block_addr;  // byte offset within the cache line

        // Step 1: Create request token (mem_fetch — no data payload)
        mem_access_sector_mask_t sm; sm.set(0);
        mem_access_t access(GLOBAL_ACC_R, addr, size, false,
                            active_mask_t(), mem_access_byte_mask_t(), sm);
        warp_inst_t *inst = new warp_inst_t();
        inst->m_is_load = true;
        mem_fetch *mf = new mem_fetch(access, inst, 0, 0, 0, 0, 0, NULL, cycle);

        // Step 2: TIMING — send request through cache pipeline
        std::list<cache_event> events;
        enum cache_request_status status =
            m_cache->access(mf->get_addr(), mf, cycle, events);

        // Step 3: TIMING — advance cache clock
        m_cache->cycle();

        // Step 4: Process miss → DRAM → fill
        bool is_hit = false;
        std::vector<uint8_t> result;

        if (status == HIT || status == HIT_RESERVED) {
            // Cache hit: data exists in L1 DataStore (FUNCTIONAL)
            is_hit = true;
            if (l1_data.contains(block_addr)) {
                auto full = l1_data.read(block_addr, m_cfg.get_line_sz());
                result.assign(full.begin() + offset,
                              full.begin() + offset + size);
            } else {
                // First access to this block — was pre-loaded into dram_data
                // but never filled into l1_data yet (e.g. warm-up scenario)
                auto full = dram_data.read(block_addr, m_cfg.get_line_sz());
                result.assign(full.begin() + offset,
                              full.begin() + offset + size);
            }
        } else if (status == MISS || status == SECTOR_MISS) {
            // Cache miss: fetch from DRAM DataStore → L1 DataStore
            is_hit = false;
            auto full = dram_data.read(block_addr, m_cfg.get_line_sz());
            result.assign(full.begin() + offset,
                          full.begin() + offset + size);

            // FUNCTIONAL: copy full line from DRAM to L1
            l1_data.write(block_addr, full.data(), full.size());

            // Send miss queue to DRAM interface (already done by cycle())
            // Process DRAM response → fill cache (TIMING)
            while (!dram_if.queue.empty()) {
                mem_fetch *resp = dram_if.queue.front();
                dram_if.queue.pop_front();
                m_cache->fill(resp, cycle);
            }
        } else {
            // RESERVATION_FAIL — retry next cycle
            result = std::vector<uint8_t>(size, 0);
        }

        // Step 5: Drain ready requests
        while (m_cache->access_ready()) {
            m_cache->next_access();
        }

        return {is_hit, result};
    }

    /// Write `size` bytes at `addr` to L1 DataStore (functional).
    /// Performs read-modify-write within the cache line so existing
    /// bytes at other offsets are preserved.
    /// Note: this example uses read_only_cache, so writes go directly to
    /// the DataStore without cache tag update.  Use data_cache for full
    /// write policy support (write-back, write-through, etc.).
    void write(new_addr_type addr, const uint8_t* data, unsigned size) {
        new_addr_type block_addr = addr & ~(new_addr_type)(m_cfg.get_line_sz() - 1);
        unsigned offset = addr - block_addr;
        unsigned line_sz = m_cfg.get_line_sz();

        // Read existing block (or zeros if not present)
        std::vector<uint8_t> block;
        if (l1_data.contains(block_addr))
            block = l1_data.read(block_addr, line_sz);
        else if (dram_data.contains(block_addr))
            block = dram_data.read(block_addr, line_sz);
        else
            block.assign(line_sz, 0);

        // Apply write at offset
        for (unsigned i = 0; i < size && (offset + i) < line_sz; i++)
            block[offset + i] = data[i];

        l1_data.write(block_addr, block.data(), line_sz);
    }

    /// Flush: copy all dirty data from L1 to DRAM DataStore.
    /// In a real system this would use cache.flush() + walk dirty blocks.
    void flush_to_dram() {
        m_cache->flush();
    }

    /// Access the underlying cache for stats
    read_only_cache& cache() { return *m_cache; }
    const cache_config& config() const { return m_cfg; }
};

#endif // MEMORY_SYSTEM_H
