#ifndef OPEN_CACHE_MSHR_H
#define OPEN_CACHE_MSHR_H

#include "open_cache_types.h"
#include <unordered_map>
#include <list>
#include <cstdio>
#include <cassert>

namespace opencache {

// Miss Status Holding Register table
// Tracks all outstanding cache misses
class MSHRTable {
public:
    MSHRTable(uint32_t num_entries, uint32_t max_merge)
        : m_num_entries(num_entries), m_max_merged(max_merge) {}

    // Check if there's a pending request for this block address
    bool probe(addr_t block_addr) const {
        return m_data.find(block_addr) != m_data.end();
    }

    // Check if MSHR is full (no more entries available for new block)
    bool full(addr_t block_addr) const {
        auto it = m_data.find(block_addr);
        if (it != m_data.end()) {
            // Already tracking — check merge limit
            return it->second.requests.size() >= m_max_merged;
        }
        return m_data.size() >= m_num_entries;
    }

    // Add or merge an access to the MSHR
    bool add(addr_t block_addr, const CacheRequest &req) {
        auto it = m_data.find(block_addr);

        if (it != m_data.end()) {
            // Merge with existing entry
            if (it->second.requests.size() >= m_max_merged) {
                return false;
            }
            it->second.requests.push_back(req);
            return true;
        }

        // New entry
        if (m_data.size() >= m_num_entries) {
            return false;
        }

        MSHREntry entry;
        entry.requests.push_back(req);
        entry.ready = false;
        entry.has_atomic = false;
        m_data[block_addr] = entry;
        return true;
    }

    // Mark an entry as ready (fill response received)
    void mark_ready(addr_t block_addr) {
        auto it = m_data.find(block_addr);
        if (it != m_data.end()) {
            it->second.ready = true;
            m_ready_list.push_back(block_addr);
        }
    }

    // Check if any entry is ready
    bool access_ready() const {
        return !m_ready_list.empty();
    }

    // Get number of ready entries
    size_t ready_count() const { return m_ready_list.size(); }

    // Get number of active entries
    size_t active_count() const { return m_data.size(); }

    // Get the next ready access (returns empty request if none)
    std::vector<CacheRequest> next_ready() {
        if (m_ready_list.empty()) return {};

        addr_t addr = m_ready_list.front();
        m_ready_list.pop_front();

        auto it = m_data.find(addr);
        if (it != m_data.end()) {
            std::vector<CacheRequest> result = std::move(it->second.requests);
            m_data.erase(it);
            return result;
        }
        return {};
    }

    // Check if an entry has pending read-after-write
    bool is_read_after_write_pending(addr_t block_addr) const {
        auto it = m_data.find(block_addr);
        if (it == m_data.end()) return false;

        bool has_write = false;
        bool has_read = false;
        for (const auto &req : it->second.requests) {
            if (req.is_write()) has_write = true;
            if (req.is_read()) has_read = true;
        }
        return has_write && has_read;
    }

    void print(FILE *fp = stdout) const {
        fprintf(fp, "MSHR: %zu/%u entries used, %zu ready\n",
                m_data.size(), m_num_entries, m_ready_list.size());
    }

    // Clear all entries
    void clear() {
        m_data.clear();
        m_ready_list.clear();
    }

private:
    uint32_t m_num_entries;
    uint32_t m_max_merged;

    struct MSHREntry {
        std::vector<CacheRequest> requests;
        bool ready;
        bool has_atomic;
    };

    std::unordered_map<addr_t, MSHREntry> m_data;
    std::list<addr_t> m_ready_list;
};

} // namespace opencache

#endif // OPEN_CACHE_MSHR_H
