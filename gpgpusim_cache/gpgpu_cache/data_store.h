// =============================================================================
// DataStore — Functional memory model for GPGPU-Sim Cache Reference
//
// In GPGPU-Sim, actual data bytes live in gpgpu_t::m_global_mem (a memory_space
// object separate from the cache timing model).  The cache (tag_array, MSHR,
// bandwidth_management) only tracks addresses, states, and delays — it never
// stores the data itself.
//
// DataStore provides that "functional memory" layer for the standalone
// reference.  One instance is created per memory level (L1$, L2$, DRAM).
// The cache model (tag + state + timing) and the DataStore (actual bytes)
// remain cleanly separated — exactly as in the real GPGPU-Sim.
//
// Usage pattern (GPGPU-Sim style):
//   1. Pre-populate DRAM DataStore with initial data
//   2. On cache MISS → lower level returns mem_fetch token (TIMING)
//   3. On fill, copy data from lower DataStore to upper DataStore (FUNCTIONAL)
//   4. On cache HIT, read data from this level's DataStore (FUNCTIONAL)
//   5. On eviction, copy dirty data to lower DataStore (FUNCTIONAL)
// =============================================================================

#ifndef DATA_STORE_H
#define DATA_STORE_H

#include <map>
#include <vector>
#include <cstdint>
#include <cstring>

typedef unsigned long long new_addr_type;

class DataStore {
public:
    DataStore() {}

    // Write data bytes at the given address.
    // Data is stored keyed by block_addr (cache-line-aligned address).
    void write(new_addr_type block_addr, const uint8_t* data, unsigned size) {
        std::vector<uint8_t>& entry = m_storage[block_addr];
        if (entry.size() < size) entry.resize(size);
        memcpy(entry.data(), data, size);
    }

    // Read data bytes at the given address.
    // addr is the block_addr; returns a copy of stored data.
    std::vector<uint8_t> read(new_addr_type block_addr, unsigned size) const {
        auto it = m_storage.find(block_addr);
        if (it == m_storage.end()) {
            return std::vector<uint8_t>(size, 0);  // default: all zeros
        }
        std::vector<uint8_t> result(it->second.begin(), it->second.end());
        if (result.size() < size) result.resize(size, 0);
        return result;
    }

    // Check whether data has been stored at this block_addr.
    bool contains(new_addr_type block_addr) const {
        return m_storage.find(block_addr) != m_storage.end();
    }

    // Clear all stored data.
    void clear() { m_storage.clear(); }

    // Number of distinct cached addresses.
    size_t size() const { return m_storage.size(); }

private:
    // Key = block-aligned address, Value = data bytes
    std::map<new_addr_type, std::vector<uint8_t>> m_storage;
};

#endif // DATA_STORE_H
