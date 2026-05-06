#ifndef OPEN_CACHE_H
#define OPEN_CACHE_H

#include "open_cache_types.h"
#include "cache_config.h"
#include "cache_block.h"
#include "tag_array.h"
#include "mshr.h"
#include "cache_stats.h"

#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <cstdio>

namespace opencache {

// Forward declarations
class CacheMemoryInterface;

// Memory interface — abstract lower-level memory connection
// Implement this to connect caches to each other or to main memory
class CacheMemoryInterface {
public:
    virtual ~CacheMemoryInterface() = default;

    // Send a request to the lower level
    virtual bool send_request(const CacheRequest &req) = 0;

    // Check if the lower level can accept a request
    virtual bool can_accept_request() const { return true; }

    // Get fill latency from lower level
    virtual uint32_t get_fill_latency() const { return 10; }
};

// Simple memory interface for standalone testing
class SimpleMemory : public CacheMemoryInterface {
public:
    SimpleMemory(uint32_t latency = 100) : m_latency(latency) {}

    bool send_request(const CacheRequest & /*req*/) override {
        m_requests_served++;
        return true;
    }

    bool can_accept_request() const override { return true; }
    uint32_t get_fill_latency() const override { return m_latency; }

    uint64_t get_requests_served() const { return m_requests_served; }

private:
    uint32_t m_latency;
    uint64_t m_requests_served = 0;
};


// ===== baseline_cache =====
// Implements common functionality for all cache types
class BaselineCache {
public:
    BaselineCache(const std::string &name, const CacheConfig &config,
                  int core_id, int type_id,
                  CacheMemoryInterface *memport,
                  CacheLevel level);
    virtual ~BaselineCache();

    // Main access interface
    virtual CacheResult access(const CacheRequest &req);

    // Cycle the cache (for timing model)
    virtual void cycle();

    // Fill response from lower level
    virtual void fill(const CacheRequest &req, uint64_t time);

    // Check if a previously missed access is now ready
    bool access_ready() const { return m_mshrs.access_ready(); }

    // Get next ready access
    std::vector<CacheRequest> next_ready() { return m_mshrs.next_ready(); }

    // Bandwidth availability
    bool data_port_free() const { return m_data_port_available; }
    bool fill_port_free() const { return m_fill_port_available; }

    // Flush & invalidate
    void flush() { m_tag_array->flush(); }
    void invalidate() { m_tag_array->invalidate(); }

    // Statistics
    const CacheStats &get_stats() const { return m_stats; }
    void get_sub_stats(CacheSubStats &css) const { m_stats.get_sub_stats(css); }
    void print_stats(FILE *fp = stdout) const;
    void print_config(FILE *fp = stdout) const;

    // Access config
    CacheConfig &get_config() { return m_config; }
    const CacheConfig &get_config() const { return m_config; }
    const std::string &get_name() const { return m_name; }

protected:
    std::string m_name;
    CacheConfig m_config;  // owned by value — TagArray references this
    TagArray *m_tag_array;
    MSHRTable m_mshrs;
    CacheLevel m_level;

    CacheMemoryInterface *m_memport;
    CacheStats m_stats;

    // Port bandwidth management
    bool m_data_port_available = true;
    bool m_fill_port_available = true;
    int m_data_port_occupied_cycles = 0;
    int m_fill_port_occupied_cycles = 0;

    // Miss queue
    std::list<CacheRequest> m_miss_queue;

    // Extra fields for tracking miss info
    struct ExtraFields {
        bool valid = false;
        addr_t block_addr = 0;
        addr_t address = 0;
        uint32_t cache_index = 0;
        uint32_t data_size = 0;
        uint32_t pending_read = 0;
    };
    std::unordered_map<const CacheRequest *, ExtraFields> m_extra_fields;

    void replenish_ports();
    bool miss_queue_full(uint32_t num_miss) const;

    // Send a read request to lower level
    void send_read_request(addr_t addr, addr_t block_addr,
                           uint32_t cache_index, const CacheRequest &req,
                           uint64_t time, bool read_only, bool wa,
                           bool &do_miss, std::vector<CacheEvent> &events);
};


// ===== read_only_cache =====
class ReadOnlyCache : public BaselineCache {
public:
    ReadOnlyCache(const std::string &name, const CacheConfig &config,
                  int core_id, int type_id,
                  CacheMemoryInterface *memport,
                  CacheLevel level = CacheLevel::L1);

    CacheResult access(const CacheRequest &req) override;
};


// ===== data_cache =====
// Implements read/write cache with configurable write policies
class DataCache : public BaselineCache {
public:
    DataCache(const std::string &name, const CacheConfig &config,
              int core_id, int type_id,
              CacheMemoryInterface *memport,
              CacheLevel level = CacheLevel::L1);

    CacheResult access(const CacheRequest &req) override;

protected:
    // Internal access processing
    CacheResult process_access(const CacheRequest &req, uint64_t time);

    // Write-hit handlers
    CacheResult write_hit_writeback(const CacheRequest &req, uint64_t time,
                                     uint32_t set_index, uint32_t way_index,
                                     uint32_t flat_idx);
    CacheResult write_hit_writethrough(const CacheRequest &req, uint64_t time,
                                        uint32_t set_index, uint32_t way_index,
                                        uint32_t flat_idx);
    CacheResult write_hit_writeevict(const CacheRequest &req, uint64_t time,
                                      uint32_t set_index, uint32_t flat_idx);

    // Write-miss handlers
    CacheResult write_miss_no_wa(const CacheRequest &req, uint64_t time);
    CacheResult write_miss_wa_naive(const CacheRequest &req, uint64_t time);
    CacheResult write_miss_wa_fetch_on_write(const CacheRequest &req, uint64_t time);
    CacheResult write_miss_wa_lazy_fetch_on_read(const CacheRequest &req, uint64_t time);

    // Read handlers
    CacheResult read_hit(const CacheRequest &req, uint64_t time,
                         uint32_t set_index, uint32_t way_index,
                         uint32_t flat_idx);
    CacheResult read_miss(const CacheRequest &req, uint64_t time,
                          TagProbeResult &probe);

};

} // namespace opencache

#endif // OPEN_CACHE_H
