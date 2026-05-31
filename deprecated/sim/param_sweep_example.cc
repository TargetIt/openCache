// openCache parameter sweep example
// Demonstrates how to use openCache for design space exploration
//
// Build: g++ -std=c++17 -I../src -I../scenario \
//        sim/param_sweep_example.cc src/*.cc scenario/*.cc -o param_sweep

#include "../src/open_cache.h"
#include "../scenario/scenarios.h"
#include <cstdio>
#include <vector>
#include <algorithm>

using namespace opencache;

struct SweepResult {
    uint32_t num_sets;
    uint32_t line_size;
    uint32_t associativity;
    uint32_t num_banks;
    uint32_t total_size_kb;
    double hit_rate;
    double area_estimate; // rough area proxy
};

// Run a trace against a cache configuration
double run_trace(DataCache &cache, const std::vector<addr_t> &addresses,
                 const std::vector<bool> &is_write) {
    for (size_t i = 0; i < addresses.size(); i++) {
        AccessType type = is_write[i] ? AccessType::WRITE : AccessType::READ;
        CacheRequest req(addresses[i], type, 4);
        cache.access(req);
    }
    CacheSubStats stats;
    cache.get_sub_stats(stats);
    return stats.hit_rate();
}

// Generate synthetic trace with temporal + spatial locality
std::pair<std::vector<addr_t>, std::vector<bool>> generate_trace(size_t length) {
    std::vector<addr_t> addrs;
    std::vector<bool> is_write;
    addrs.reserve(length);
    is_write.reserve(length);

    addr_t working_set_base = 0;
    size_t working_set_size = 64; // 64 addresses in working set
    size_t switch_period = 200;

    for (size_t i = 0; i < length; i++) {
        // Periodically shift working set
        if (i > 0 && (i % switch_period) == 0) {
            working_set_base += 4096 + (i * 7) % 32768;
        }

        // 80% within working set, 20% random
        addr_t addr;
        if ((i * 137) % 100 < 80) {
            addr = working_set_base + ((i * 17) % working_set_size) * 64;
        } else {
            addr = (i * 9973 + 12345) & 0xFFFFF;
        }

        addrs.push_back(addr);
        is_write.push_back((i % 5) == 0); // 20% writes
    }

    return {addrs, is_write};
}

int main() {
    printf("=== openCache Parameter Sweep ===\n\n");

    auto [addresses, is_write] = generate_trace(10000);
    printf("Generated trace: %zu accesses (%zu%% reads, %zu%% writes)\n\n",
           addresses.size(),
           std::count(is_write.begin(), is_write.end(), false) * 100 / is_write.size(),
           std::count(is_write.begin(), is_write.end(), true) * 100 / is_write.size());

    // Define parameter ranges to sweep
    std::vector<uint32_t> set_options = {16, 32, 64, 128, 256};
    std::vector<uint32_t> way_options = {1, 2, 4, 8, 16};
    std::vector<uint32_t> line_options = {32, 64, 128};
    std::vector<uint32_t> bank_options = {1, 2, 4, 8};
    std::vector<ReplacementPolicy> rp_options = {
        ReplacementPolicy::LRU, ReplacementPolicy::FIFO, ReplacementPolicy::RANDOM
    };
    std::vector<WritePolicy> wp_options = {
        WritePolicy::WRITE_BACK, WritePolicy::WRITE_THROUGH, WritePolicy::WRITE_EVICT
    };

    std::vector<SweepResult> results;

    printf("Sweeping cache size configurations...\n");
    printf("%-6s %-6s %-6s %-6s %-8s %10s %12s\n",
           "Sets", "Ways", "Line", "Banks", "SizeKB", "HitRate%", "AreaProxy");

    // Sweep key geometric parameters
    for (auto nsets : set_options) {
        for (auto nways : way_options) {
            uint32_t lsize = 128; // fixed line size
            uint32_t nbanks = 4;  // fixed banks
            CacheConfig config(nsets, lsize, nways);
            config.num_banks = nbanks;
            config.write_policy = WritePolicy::WRITE_BACK;
            config.hit_latency = 1;
            config.fill_latency = 50;

            SimpleMemory mem(100);
            DataCache cache("Sweep", config, 0, 0, &mem, CacheLevel::L1);

            double hr = run_trace(cache, addresses, is_write);

            SweepResult r;
            r.num_sets = nsets;
            r.line_size = lsize;
            r.associativity = nways;
            r.num_banks = nbanks;
            r.total_size_kb = config.get_total_size_kb();
            r.hit_rate = hr;
            r.area_estimate = static_cast<double>(r.total_size_kb) * nways; // rough proxy
            results.push_back(r);

            printf("%-6u %-6u %-6u %-6u %-8u %9.2f%% %12.1f\n",
                   nsets, nways, lsize, nbanks,
                   config.get_total_size_kb(),
                   hr * 100.0, r.area_estimate);
        }
    }

    // Find Pareto-optimal configurations
    printf("\n=== Pareto-Optimal Configurations (hit-rate vs area) ===\n");
    printf("%-8s %-10s %-10s\n", "SizeKB", "HitRate%", "AreaProxy");

    // Sort by area, find increasing hit rates
    std::sort(results.begin(), results.end(),
              [](const SweepResult &a, const SweepResult &b) {
                  return a.area_estimate < b.area_estimate;
              });

    double max_hr = -1.0;
    for (const auto &r : results) {
        if (r.hit_rate > max_hr + 0.001) {
            max_hr = r.hit_rate;
            printf("%-8u %9.2f%% %10.1f  (sets=%u ways=%u line=%u)\n",
                   r.total_size_kb, r.hit_rate * 100.0,
                   r.area_estimate, r.num_sets, r.associativity, r.line_size);
        }
    }

    // Sweep write policy for fixed geometry
    printf("\n=== Write Policy Comparison (64-set, 4-way, 128B line) ===\n");
    printf("%-20s %12s %12s\n", "Policy", "HitRate%", "WriteMiss%");

    for (auto wp : wp_options) {
        CacheConfig config(64, 128, 4);
        config.write_policy = wp;
        config.write_alloc_policy = WriteAllocatePolicy::FETCH_ON_WRITE;
        if (wp == WritePolicy::WRITE_EVICT) {
            config.write_alloc_policy = WriteAllocatePolicy::NO_WRITE_ALLOCATE;
        }
        SimpleMemory mem(100);
        DataCache cache("WP_Sweep", config, 0, 0, &mem, CacheLevel::L1);

        double hr = run_trace(cache, addresses, is_write);
        CacheSubStats stats;
        cache.get_sub_stats(stats);

        const char *wp_name = "???";
        if (wp == WritePolicy::WRITE_BACK) wp_name = "WRITE_BACK";
        else if (wp == WritePolicy::WRITE_THROUGH) wp_name = "WRITE_THROUGH";
        else if (wp == WritePolicy::WRITE_EVICT) wp_name = "WRITE_EVICT";

        printf("%-20s %11.2f%% %11.2f%%\n",
               wp_name, hr * 100.0, stats.write_hit_rate() * 100.0);
    }

    // Sweep replacement policy for fixed geometry
    printf("\n=== Replacement Policy Comparison (64-set, 4-way, 128B line) ===\n");
    printf("%-20s %12s\n", "Policy", "HitRate%");

    for (auto rp : rp_options) {
        CacheConfig config(64, 128, 4, rp, WritePolicy::WRITE_BACK);
        SimpleMemory mem(100);
        DataCache cache("RP_Sweep", config, 0, 0, &mem, CacheLevel::L1);

        double hr = run_trace(cache, addresses, is_write);

        const char *rp_name = "???";
        if (rp == ReplacementPolicy::LRU) rp_name = "LRU";
        else if (rp == ReplacementPolicy::FIFO) rp_name = "FIFO";
        else if (rp == ReplacementPolicy::RANDOM) rp_name = "RANDOM";

        printf("%-20s %11.2f%%\n", rp_name, hr * 100.0);
    }

    printf("\n=== Sweep Complete ===\n");
    return 0;
}
