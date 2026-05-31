// =============================================================================
// Ported from GPGPU-Sim (gpgpu-sim_distribution)
//   gpu-cache.h:1088-1142  struct cache_sub_stats     → CacheSubStats
//   gpu-cache.h:1204-1259  class cache_stats          → CacheStats
//   gpu-cache.cc:642-653   cache_stats::clear()
//   gpu-cache.cc:662-681   cache_stats::inc_stats()   → record_access()
//   gpu-cache.cc:704-720   cache_stats::inc_fail_stats() → record_fail()
//   gpu-cache.cc:722-917   cache_stats::select_stats_status()
//   gpu-cache.cc:1021-1054 cache_stats::get_sub_stats()
//   gpu-cache.cc:1134-1151 cache_stats::sample_cache_port_utility()
//
// NOT PORTED (GPU-specific):
//   gpu-cache.h:1144-1202  cache_sub_stats_pw (AerialVision per-window stats)
//   gpu-cache.cc:655-660   clear_pw()
//   gpu-cache.cc:683-702   inc_stats_pw()
//   gpu-cache.cc:1055-1132 get_sub_stats_pw()
//   operator() indexer, operator+ aggregation
//
// Key modifications:
//   - Single stream only (stream_id=0); GPGPU-Sim multi-stream via std::map
//   - CacheSubStats adds: read_hits/write_hits/read_misses/write_misses/sector_misses
//   - CacheSubStats adds: hit_rate(), read_hit_rate(), write_hit_rate()
//   - record_access() auto-computes read/write hit/miss from AccessType
//   - m_fail_stats = std::vector<uint64_t>(8, 0)
//     [v2 fix: was {8,0} causing 2-element vector + heap overflow]
//   - Removed: per-stream stats, per-window stats, operator overloads
// =============================================================================

#ifndef OPEN_CACHE_STATS_H
#define OPEN_CACHE_STATS_H

#include "open_cache_types.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <map>

namespace opencache {

struct CacheSubStats {
    uint64_t accesses = 0;
    uint64_t misses = 0;
    uint64_t pending_hits = 0;
    uint64_t res_fails = 0;
    uint64_t sector_misses = 0;

    uint64_t read_hits = 0;
    uint64_t read_misses = 0;
    uint64_t write_hits = 0;
    uint64_t write_misses = 0;

    uint64_t port_available_cycles = 0;
    uint64_t data_port_busy_cycles = 0;
    uint64_t fill_port_busy_cycles = 0;

    void clear() {
        accesses = 0;
        misses = 0;
        pending_hits = 0;
        res_fails = 0;
        sector_misses = 0;
        read_hits = 0;
        read_misses = 0;
        write_hits = 0;
        write_misses = 0;
        port_available_cycles = 0;
        data_port_busy_cycles = 0;
        fill_port_busy_cycles = 0;
    }

    CacheSubStats &operator+=(const CacheSubStats &other) {
        accesses += other.accesses;
        misses += other.misses;
        pending_hits += other.pending_hits;
        res_fails += other.res_fails;
        sector_misses += other.sector_misses;
        read_hits += other.read_hits;
        read_misses += other.read_misses;
        write_hits += other.write_hits;
        write_misses += other.write_misses;
        port_available_cycles += other.port_available_cycles;
        data_port_busy_cycles += other.data_port_busy_cycles;
        fill_port_busy_cycles += other.fill_port_busy_cycles;
        return *this;
    }

    double hit_rate() const {
        return accesses > 0 ? 1.0 - static_cast<double>(misses) / accesses : 0.0;
    }

    double read_hit_rate() const {
        uint64_t total = read_hits + read_misses;
        return total > 0 ? static_cast<double>(read_hits) / total : 0.0;
    }

    double write_hit_rate() const {
        uint64_t total = write_hits + write_misses;
        return total > 0 ? static_cast<double>(write_hits) / total : 0.0;
    }
};

class CacheStats {
public:
    CacheStats() { clear(); }

    void clear() {
        m_stats[0].clear(); // stream 0 is default
        m_fail_stats.clear();
    }

    void record_access(AccessType type, AccessStatus outcome) {
        auto &st = m_stats[0];

        bool is_write = (type == AccessType::WRITE ||
                         type == AccessType::WRITE_BACK ||
                         type == AccessType::WRITE_ALLOCATE);

        st.accesses++;
        switch (outcome) {
            case AccessStatus::HIT:
                if (is_write) st.write_hits++;
                else st.read_hits++;
                break;
            case AccessStatus::HIT_RESERVED:
                st.pending_hits++;
                break;
            case AccessStatus::MISS:
                st.misses++;
                if (is_write) st.write_misses++;
                else st.read_misses++;
                break;
            case AccessStatus::SECTOR_MISS:
                st.sector_misses++;
                break;
            case AccessStatus::RESERVATION_FAIL:
                st.res_fails++;
                break;
            case AccessStatus::MSHR_HIT:
                // MSHR hit is still counted as a miss
                st.misses++;
                if (is_write) st.write_misses++;
                else st.read_misses++;
                break;
            default:
                break;
        }
    }

    void record_fail(AccessType type, ReservationFailReason reason) {
        // Track reservation failure reasons
        m_fail_stats[static_cast<int>(reason)]++;
    }

    // Select proper stats outcome when probe and access status differ
    // (e.g. HIT_RESERVED counted differently at cache vs core level)
    AccessStatus select_stats_status(AccessStatus probe_status,
                                      AccessStatus access_status) const {
        if (probe_status == AccessStatus::HIT_RESERVED &&
            access_status != AccessStatus::RESERVATION_FAIL)
            return probe_status;
        else if (probe_status == AccessStatus::SECTOR_MISS &&
                 access_status == AccessStatus::MISS)
            return probe_status;
        else
            return access_status;
    }

    void sample_port_utility(bool data_port_busy, bool fill_port_busy) {
        auto &st = m_stats[0];
        st.port_available_cycles++;
        if (data_port_busy) st.data_port_busy_cycles++;
        if (fill_port_busy) st.fill_port_busy_cycles++;
    }

    void get_sub_stats(CacheSubStats &css) const {
        css = m_stats.at(0);
    }

    void print(FILE *fp = stdout, const char *name = "Cache") const {
        const auto &st = m_stats.at(0);
        fprintf(fp, "\n=== %s Statistics ===\n", name);
        fprintf(fp, "  Accesses:        %llu\n", (unsigned long long)st.accesses);
        fprintf(fp, "  Misses:          %llu\n", (unsigned long long)st.misses);
        fprintf(fp, "  Hit Rate:        %.2f%%\n", st.hit_rate() * 100.0);
        fprintf(fp, "  Read Hits:       %llu, Read Misses:  %llu (%.2f%%)\n",
                (unsigned long long)st.read_hits, (unsigned long long)st.read_misses,
                st.read_hit_rate() * 100.0);
        fprintf(fp, "  Write Hits:      %llu, Write Misses: %llu (%.2f%%)\n",
                (unsigned long long)st.write_hits, (unsigned long long)st.write_misses,
                st.write_hit_rate() * 100.0);
        fprintf(fp, "  Pending Hits:    %llu\n", (unsigned long long)st.pending_hits);
        fprintf(fp, "  Reservation Fail:%llu\n", (unsigned long long)st.res_fails);
        fprintf(fp, "  Sector Misses:   %llu\n", (unsigned long long)st.sector_misses);
    }

private:
    // stream_id -> stats (default stream 0)
    std::map<uint32_t, CacheSubStats> m_stats;
    std::vector<uint64_t> m_fail_stats = std::vector<uint64_t>(8, 0);
};

} // namespace opencache

#endif // OPEN_CACHE_STATS_H
