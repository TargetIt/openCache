#include "../src/open_cache.h"
#include "../scenario/scenarios.h"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <vector>
#include <cmath>

using namespace opencache;

// Simple test runner
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  RUN  %s ... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED\n  ASSERT_EQ(%s, %s): %llu != %llu\n", \
               #a, #b, (unsigned long long)(a), (unsigned long long)(b)); \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n  ASSERT_TRUE(%s)\n", #cond); \
        return; \
    } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (std::fabs((a) - (b)) > (eps)) { \
        printf("FAILED\n  ASSERT_FLOAT_EQ(%s, %s): %f != %f\n", \
               #a, #b, (double)(a), (double)(b)); \
        return; \
    } \
} while(0)

// ===== Test: Basic Configuration =====
TEST(config_parse_gpgpu_sim_format) {
    CacheConfig config;

    // Typical L1 cache config: S:32:128:4,L:L:m:W:X,A:32:4,64
    bool ok = config.parse_config_string("S:32:128:4,L:L:m:W:X,A:32:4,64");
    ASSERT_TRUE(ok);
    ASSERT_EQ((int)config.cache_type, (int)CacheType::SECTOR);
    ASSERT_EQ(config.num_sets, 32u);
    ASSERT_EQ(config.line_size, 128u);
    ASSERT_EQ(config.associativity, 4u);
    ASSERT_EQ((int)config.replacement_policy, (int)ReplacementPolicy::LRU);
    ASSERT_EQ((int)config.write_policy, (int)WritePolicy::LOCAL_WB_GLOBAL_WT);

    // Typical L2 cache: N:256:128:16,L:B:m:F:X,A:64:8,128
    CacheConfig config2;
    ok = config2.parse_config_string("N:256:128:16,L:B:m:F:X,A:64:8,128");
    ASSERT_TRUE(ok);
    ASSERT_EQ((int)config2.cache_type, (int)CacheType::NORMAL);
    ASSERT_EQ(config2.num_sets, 256u);
    ASSERT_EQ((int)config2.write_policy, (int)WritePolicy::WRITE_BACK);
    ASSERT_EQ((int)config2.write_alloc_policy, (int)WriteAllocatePolicy::FETCH_ON_WRITE);
}

TEST(config_simplified_format) {
    CacheConfig config;
    bool ok = config.parse_config_string("64:64:8");
    ASSERT_TRUE(ok);
    ASSERT_EQ(config.num_sets, 64u);
    ASSERT_EQ(config.line_size, 64u);
    ASSERT_EQ(config.associativity, 8u);
}

TEST(config_derived_parameters) {
    CacheConfig config(64, 128, 4, ReplacementPolicy::LRU, WritePolicy::WRITE_BACK);
    ASSERT_EQ(config.get_total_size_kb(), 32u);  // 64*4*128 / 1024 = 32KB
    ASSERT_EQ(config.get_num_lines(), 256u);
    ASSERT_EQ(config.line_size_log2, 7u);
    ASSERT_EQ(config.num_sets_log2, 6u);
}

TEST(config_set_index) {
    CacheConfig config(64, 64, 4);
    // set_index = (addr >> line_size_log2) & (num_sets - 1)
    // line_size_log2=6, num_sets=64 => mask=63
    addr_t addr = 64; // exactly 1 set offset from 0
    uint32_t set_idx = config.get_set_index(addr);
    ASSERT_EQ(set_idx, 1u);
}

// ===== Test: Tag Array Operations =====
TEST(tag_array_basic) {
    CacheConfig config(4, 64, 2, ReplacementPolicy::LRU, WritePolicy::WRITE_BACK);

    TagArray tags(config, 0, 0);

    // Initial probe should miss
    auto result = tags.probe(0, false);
    ASSERT_EQ((int)result.status, (int)AccessStatus::MISS);

    // Access (allocate)
    bool wb;
    EvictedBlockInfo evicted;
    auto access_result = tags.access(0, 1, false, wb, evicted);
    ASSERT_EQ((int)access_result.status, (int)AccessStatus::MISS);
    ASSERT_TRUE(!wb);

    // Fill
    tags.fill(access_result.set_index * config.associativity + access_result.way_index,
              2, false);

    // Now probe should hit
    auto result2 = tags.probe(0, false);
    ASSERT_EQ((int)result2.status, (int)AccessStatus::HIT);
}

TEST(tag_array_lru_replacement) {
    // 2 sets, 64B line, 2-way. All test addresses map to set 0.
    // set_index = (addr >> 6) & 1, so addresses 0, 128, 256, ... map to set 0.
    CacheConfig config(2, 64, 2, ReplacementPolicy::LRU, WritePolicy::WRITE_BACK);
    TagArray tags(config, 0, 0);

    bool wb;
    EvictedBlockInfo evicted;

    // Fill set 0 way 0 with address 0
    auto r1 = tags.access(0, 1, false, wb, evicted);
    ASSERT_EQ(r1.set_index, 0u);
    tags.fill(r1.set_index * 2 + r1.way_index, 2, false);

    // Fill set 0 way 1 with address 128 (same set 0, different tag)
    auto r2 = tags.access(128, 2, false, wb, evicted);
    ASSERT_EQ(r2.set_index, 0u);
    tags.fill(r2.set_index * 2 + r2.way_index, 3, false);

    // Access address 0 again — should hit (LRU promotes it)
    auto r3 = tags.access(0, 4, false, wb, evicted);
    ASSERT_EQ((int)r3.status, (int)AccessStatus::HIT);

    // Access address 256 — must evict LRU, which is address 128 (accessed at time 2)
    auto r4 = tags.access(256, 5, false, wb, evicted);
    ASSERT_EQ((int)r4.status, (int)AccessStatus::MISS);
    tags.fill(r4.set_index * 2 + r4.way_index, 6, false);

    // Address 128 should now be evicted
    auto r5 = tags.probe(128, false);
    ASSERT_EQ((int)r5.status, (int)AccessStatus::MISS);

    // But address 0 should still be present (accessed more recently)
    auto r6 = tags.probe(0, false);
    ASSERT_EQ((int)r6.status, (int)AccessStatus::HIT);
}

TEST(tag_array_stats) {
    CacheConfig config(8, 64, 2);
    TagArray tags(config, 0, 0);

    // 10 accesses, each to a different address → all misses
    for (int i = 0; i < 10; i++) {
        addr_t addr = i * 128; // different addresses, no conflicts
        bool wb;
        EvictedBlockInfo evicted;
        tags.access(addr, i, false, wb, evicted);

        uint32_t set_idx = config.get_set_index(addr);
        // Fill allocated entry
        for (uint32_t w = 0; w < config.associativity; w++) {
            auto *block = tags.get_block(set_idx * config.associativity + w);
            if (!block->is_invalid() && block->m_tag == config.get_tag(addr)) {
                tags.fill(set_idx * config.associativity + w, i + 1, false);
                break;
            }
        }
    }

    uint32_t accesses, misses, hit_res, res_fail;
    tags.get_stats(accesses, misses, hit_res, res_fail);
    ASSERT_EQ(accesses, 10u);
    ASSERT_EQ(misses, 10u); // all are misses because all different addresses
}

// ===== Test: Cache Access Operations =====
TEST(read_only_cache_basic) {
    CacheConfig config(16, 64, 4, ReplacementPolicy::LRU, WritePolicy::READ_ONLY);
    SimpleMemory mem(20);

    ReadOnlyCache cache("TestRO", config, 0, 0, &mem, CacheLevel::L1);

    // First access: miss
    CacheRequest req1(0x1000, AccessType::READ, 4);
    auto result1 = cache.access(req1);
    ASSERT_EQ((int)result1.status, (int)AccessStatus::MISS);

    // Second access to same address (should have been filled): hit
    CacheRequest req2(0x1000, AccessType::READ, 4);
    auto result2 = cache.access(req2);
    ASSERT_EQ((int)result2.status, (int)AccessStatus::HIT);

    // Different address: miss
    CacheRequest req3(0x2000, AccessType::READ, 4);
    auto result3 = cache.access(req3);
    ASSERT_EQ((int)result3.status, (int)AccessStatus::MISS);
}

TEST(data_cache_write_back) {
    CacheConfig config(16, 64, 4, ReplacementPolicy::LRU, WritePolicy::WRITE_BACK);
    SimpleMemory mem(20);

    DataCache cache("TestWB", config, 0, 0, &mem, CacheLevel::L1);

    // Write miss: should allocate and fill
    CacheRequest req1(0x1000, AccessType::WRITE, 4);
    auto result1 = cache.access(req1);
    ASSERT_EQ((int)result1.status, (int)AccessStatus::MISS);

    // Write hit (write-back): mark modified, no eviction
    CacheRequest req2(0x1000, AccessType::WRITE, 4);
    auto result2 = cache.access(req2);
    ASSERT_EQ((int)result2.status, (int)AccessStatus::HIT);

    // Read hit to same address
    CacheRequest req3(0x1000, AccessType::READ, 4);
    auto result3 = cache.access(req3);
    ASSERT_EQ((int)result3.status, (int)AccessStatus::HIT);
}

TEST(data_cache_write_through) {
    CacheConfig config(16, 64, 4, ReplacementPolicy::LRU, WritePolicy::WRITE_THROUGH);
    SimpleMemory mem(20);

    DataCache cache("TestWT", config, 0, 0, &mem, CacheLevel::L1);

    // Write miss with no write-allocate
    CacheRequest req1(0x1000, AccessType::WRITE, 4);
    auto result1 = cache.access(req1);
    ASSERT_EQ((int)result1.status, (int)AccessStatus::MISS);
    // Should forward to memory
    ASSERT_TRUE(mem.get_requests_served() > 0);
}

TEST(data_cache_hit_rate) {
    CacheConfig config(16, 64, 2, ReplacementPolicy::LRU, WritePolicy::WRITE_BACK);
    config.write_alloc_policy = WriteAllocatePolicy::WRITE_ALLOCATE;
    SimpleMemory mem(20);

    DataCache cache("TestHR", config, 0, 0, &mem, CacheLevel::L1);

    // Access same 4 addresses repeatedly — should get high hit rate
    for (int i = 0; i < 100; i++) {
        addr_t addr = (i % 4) * 64;
        CacheRequest req(addr, i % 3 == 0 ? AccessType::WRITE : AccessType::READ, 4);
        cache.access(req);
    }

    CacheSubStats stats;
    cache.get_sub_stats(stats);

    // Hit rate should be high (> 80%)
    ASSERT_TRUE(stats.hit_rate() > 0.8);
    printf("  hit_rate=%.2f%%", stats.hit_rate() * 100.0);
}

// ===== Test: Parameter Sweep =====
TEST(param_sweep_set_count) {
    printf("\n");
    for (uint32_t nsets : {16u, 32u, 64u, 128u, 256u}) {
        CacheConfig config(nsets, 128, 4, ReplacementPolicy::LRU, WritePolicy::WRITE_BACK);
        SimpleMemory mem(20);
        DataCache cache("Sweep", config, 0, 0, &mem, CacheLevel::L1);

        // Run 1000 random accesses
        for (int i = 0; i < 1000; i++) {
            addr_t addr = (i * 137 + 42) & 0xFFFF; // pseudo-random
            CacheRequest req(addr, i % 3 == 0 ? AccessType::WRITE : AccessType::READ, 4);
            cache.access(req);
        }

        CacheSubStats stats;
        cache.get_sub_stats(stats);
        printf("    sets=%3u: hit_rate=%.2f%%\n", nsets, stats.hit_rate() * 100.0);
    }
}

TEST(param_sweep_associativity) {
    printf("\n");
    for (uint32_t assoc : {1u, 2u, 4u, 8u, 16u}) {
        CacheConfig config(64, 128, assoc, ReplacementPolicy::LRU, WritePolicy::WRITE_BACK);
        SimpleMemory mem(20);
        DataCache cache("Sweep", config, 0, 0, &mem, CacheLevel::L1);

        for (int i = 0; i < 1000; i++) {
            addr_t addr = (i * 137 + 42) & 0xFFFF;
            CacheRequest req(addr, i % 3 == 0 ? AccessType::WRITE : AccessType::READ, 4);
            cache.access(req);
        }

        CacheSubStats stats;
        cache.get_sub_stats(stats);
        printf("    assoc=%2u: hit_rate=%.2f%%\n", assoc, stats.hit_rate() * 100.0);
    }
}

TEST(param_sweep_line_size) {
    printf("\n");
    for (uint32_t lsize : {32u, 64u, 128u, 256u}) {
        CacheConfig config(64, lsize, 4, ReplacementPolicy::LRU, WritePolicy::WRITE_BACK);
        SimpleMemory mem(20);
        DataCache cache("Sweep", config, 0, 0, &mem, CacheLevel::L1);

        for (int i = 0; i < 1000; i++) {
            addr_t addr = (i * 137 + 42) & 0xFFFF;
            CacheRequest req(addr, i % 3 == 0 ? AccessType::WRITE : AccessType::READ, 4);
            cache.access(req);
        }

        CacheSubStats stats;
        cache.get_sub_stats(stats);
        printf("    lsize=%3u: hit_rate=%.2f%%\n", lsize, stats.hit_rate() * 100.0);
    }
}

TEST(param_sweep_replacement_policy) {
    printf("\n");
    for (auto rp : {ReplacementPolicy::LRU, ReplacementPolicy::FIFO, ReplacementPolicy::RANDOM}) {
        CacheConfig config(64, 128, 4, rp, WritePolicy::WRITE_BACK);
        SimpleMemory mem(20);
        DataCache cache("Sweep", config, 0, 0, &mem, CacheLevel::L1);

        for (int i = 0; i < 1000; i++) {
            addr_t addr = (i * 137 + 42) & 0xFFFF;
            CacheRequest req(addr, i % 3 == 0 ? AccessType::WRITE : AccessType::READ, 4);
            cache.access(req);
        }

        CacheSubStats stats;
        cache.get_sub_stats(stats);
        const char *rp_name = "???";
        if (rp == ReplacementPolicy::LRU) rp_name = "LRU";
        else if (rp == ReplacementPolicy::FIFO) rp_name = "FIFO";
        else if (rp == ReplacementPolicy::RANDOM) rp_name = "RANDOM";
        printf("    %s: hit_rate=%.2f%%\n", rp_name, stats.hit_rate() * 100.0);
    }
}

// ===== Test: MSHR =====
TEST(mshr_basic) {
    MSHRTable mshr(4, 4);

    // Add entries
    CacheRequest req1(0x1000, AccessType::READ, 4);
    ASSERT_TRUE(mshr.add(0x1000, req1));

    // Probe
    ASSERT_TRUE(mshr.probe(0x1000));
    ASSERT_TRUE(!mshr.probe(0x2000));

    // Mark ready
    mshr.mark_ready(0x1000);
    ASSERT_TRUE(mshr.access_ready());

    // Retrieve
    auto ready = mshr.next_ready();
    ASSERT_TRUE(!ready.empty());

    // Should be empty now
    ASSERT_TRUE(!mshr.access_ready());
}

TEST(mshr_merge) {
    MSHRTable mshr(2, 3);

    CacheRequest req1(0x1000, AccessType::READ, 4);
    ASSERT_TRUE(mshr.add(0x1000, req1));

    // Merge second request to same block
    CacheRequest req2(0x1000, AccessType::WRITE, 4);
    ASSERT_TRUE(mshr.add(0x1000, req2));

    // Merge third request
    CacheRequest req3(0x1040, AccessType::READ, 4);
    ASSERT_TRUE(mshr.add(0x1000, req3));

    // Fourth should fail (max merge = 3)
    CacheRequest req4(0x1080, AccessType::READ, 4);
    ASSERT_TRUE(!mshr.add(0x1000, req4));

    // Mark ready, should get 3 requests back
    mshr.mark_ready(0x1000);
    auto ready = mshr.next_ready();
    ASSERT_EQ(ready.size(), 3u);
}

// ===== Test: All Scenarios =====
TEST(scenario_l1_cache) {
    SimpleMemory dram(100);
    auto cache = scenario::create_l1_data_cache(&dram);

    ASSERT_TRUE(cache != nullptr);
    ASSERT_EQ((int)cache->get_config().cache_type, (int)CacheType::SECTOR);

    // Run some accesses
    for (int i = 0; i < 100; i++) {
        CacheRequest req(i * 128, i % 2 == 0 ? AccessType::READ : AccessType::WRITE, 4);
        cache->access(req);
    }

    CacheSubStats stats;
    cache->get_sub_stats(stats);
    printf("  hit_rate=%.2f%%", stats.hit_rate() * 100.0);
    ASSERT_TRUE(stats.accesses == 100);
}

TEST(scenario_l2_cache) {
    SimpleMemory dram(200);
    auto cache = scenario::create_l2_cache(&dram);

    ASSERT_TRUE(cache != nullptr);
    ASSERT_EQ((int)cache->get_config().write_policy, (int)WritePolicy::WRITE_BACK);

    for (int i = 0; i < 200; i++) {
        CacheRequest req(i * 64, i % 3 == 0 ? AccessType::WRITE : AccessType::READ, 4);
        cache->access(req);
    }

    CacheSubStats stats;
    cache->get_sub_stats(stats);
    printf("  hit_rate=%.2f%%", stats.hit_rate() * 100.0);
}

TEST(scenario_texture_cache) {
    SimpleMemory dram(50);
    auto cache = scenario::create_texture_cache(&dram);

    ASSERT_TRUE(cache != nullptr);

    // Texture accesses have spatial locality
    for (int i = 0; i < 200; i++) {
        addr_t addr = (i * 4) & 0xFFF; // small stride, high locality
        CacheRequest req(addr, AccessType::READ, 4);
        cache->access(req);
    }

    CacheSubStats stats;
    cache->get_sub_stats(stats);
    printf("  hit_rate=%.2f%%", stats.hit_rate() * 100.0);
}

TEST(scenario_write_through) {
    SimpleMemory mem(30);
    auto cache = scenario::create_write_through_cache(&mem);

    ASSERT_TRUE(cache != nullptr);

    for (int i = 0; i < 100; i++) {
        CacheRequest req(i * 64, AccessType::WRITE, 4);
        cache->access(req);
    }

    // All writes should have been forwarded
    ASSERT_TRUE(mem.get_requests_served() > 0);
}

TEST(scenario_write_back_vs_through) {
    SimpleMemory mem1(30);
    SimpleMemory mem2(30);

    auto wb_cache = scenario::create_write_back_cache(&mem1);
    auto wt_cache = scenario::create_write_through_cache(&mem2);

    // Repeated writes to same address
    for (int i = 0; i < 100; i++) {
        CacheRequest req(0x1000, AccessType::WRITE, 4);
        wb_cache->access(req);
        wt_cache->access(req);
    }

    CacheSubStats wb_stats, wt_stats;
    wb_cache->get_sub_stats(wb_stats);
    wt_cache->get_sub_stats(wt_stats);

    printf("\n    WB hit_rate=%.2f%% (memory requests=%llu)",
           wb_stats.hit_rate() * 100.0,
           (unsigned long long)mem1.get_requests_served());
    printf("\n    WT hit_rate=%.2f%% (memory requests=%llu)",
           wt_stats.hit_rate() * 100.0,
           (unsigned long long)mem2.get_requests_served());

    // Write-through should have more memory requests
    ASSERT_TRUE(mem2.get_requests_served() >= mem1.get_requests_served());
}

// ===== Test: Config String Roundtrip =====
TEST(config_roundtrip) {
    CacheConfig config(64, 128, 4, ReplacementPolicy::LRU, WritePolicy::WRITE_BACK);
    config.cache_type = CacheType::SECTOR;
    config.write_alloc_policy = WriteAllocatePolicy::FETCH_ON_WRITE;

    std::string str = config.to_config_string();
    printf("  config_string='%s'", str.c_str());

    CacheConfig config2;
    bool ok = config2.parse_config_string(str.c_str());
    ASSERT_TRUE(ok);
    ASSERT_EQ(config.num_sets, config2.num_sets);
    ASSERT_EQ(config.line_size, config2.line_size);
    ASSERT_EQ(config.associativity, config2.associativity);
    ASSERT_EQ((int)config.write_policy, (int)config2.write_policy);
}

// ===== Test: Cache Statistics Accuracy =====
TEST(stats_accuracy) {
    CacheConfig config(32, 64, 2);
    SimpleMemory mem(20);
    DataCache cache("StatsTest", config, 0, 0, &mem, CacheLevel::L1);

    // All addresses different → all misses
    for (int i = 0; i < 50; i++) {
        CacheRequest req(i * 128, AccessType::READ, 4);
        cache.access(req);
    }

    CacheSubStats stats;
    cache.get_sub_stats(stats);

    // Since addresses are all different and cache has 32*2=64 lines,
    // we should get 50 misses out of 50 accesses
    ASSERT_EQ(stats.accesses, 50u);
}

// ===== Test: Bank Interleaving =====
TEST(bank_config) {
    CacheConfig config(64, 128, 4);
    config.num_banks = 4;

    // Different addresses should map to different banks
    uint32_t b0 = config.get_bank_index(0x0000);
    uint32_t b1 = config.get_bank_index(0x0004);
    uint32_t b2 = config.get_bank_index(0x0008);
    uint32_t b3 = config.get_bank_index(0x000C);

    ASSERT_EQ(b0, 0u);
    ASSERT_EQ(b1, 1u);
    ASSERT_EQ(b2, 2u);
    ASSERT_EQ(b3, 3u);
}

// ===== Test: Set Index Hashing =====
TEST(set_index_hashing) {
    CacheConfig config_linear(64, 64, 4);
    config_linear.set_index_func = SetIndexFunction::LINEAR;

    CacheConfig config_xor(64, 64, 4);
    config_xor.set_index_func = SetIndexFunction::BITWISE_XOR;

    // Addresses that differ only in high bits (> line_size_log2 + nset_log2)
    // line_size_log2=6, num_sets=64 => nset_log2=6 => bits above 12 matter
    addr_t a1 = 0x00000;           // low address
    addr_t a2 = 0x01000;           // same low bits, high bit 12 set

    uint32_t s1 = config_linear.get_set_index(a1);
    uint32_t s2 = config_linear.get_set_index(a2);
    // Linear: both set 0 since lower 6+6 bits determine set

    uint32_t s3 = config_xor.get_set_index(a1);
    uint32_t s4 = config_xor.get_set_index(a2);
    // XOR: a1 XORs 0^0=0, a2 XORs 0^1=1 — different sets

    ASSERT_EQ(s1, s2); // Linear should give same set
    ASSERT_TRUE(s3 != s4); // XOR should give different sets
}

int main() {
    printf("\n========== openCache Test Suite ==========\n\n");

    printf("[1] Configuration Tests\n");
    RUN_TEST(config_parse_gpgpu_sim_format);
    RUN_TEST(config_simplified_format);
    RUN_TEST(config_derived_parameters);
    RUN_TEST(config_set_index);
    RUN_TEST(config_roundtrip);
    RUN_TEST(bank_config);
    RUN_TEST(set_index_hashing);

    printf("\n[2] Tag Array Tests\n");
    RUN_TEST(tag_array_basic);
    RUN_TEST(tag_array_lru_replacement);
    RUN_TEST(tag_array_stats);

    printf("\n[3] Cache Access Tests\n");
    RUN_TEST(read_only_cache_basic);
    RUN_TEST(data_cache_write_back);
    RUN_TEST(data_cache_write_through);
    RUN_TEST(data_cache_hit_rate);

    printf("\n[4] MSHR Tests\n");
    RUN_TEST(mshr_basic);
    RUN_TEST(mshr_merge);

    printf("\n[5] Scenario Tests\n");
    RUN_TEST(scenario_l1_cache);
    RUN_TEST(scenario_l2_cache);
    RUN_TEST(scenario_texture_cache);
    RUN_TEST(scenario_write_through);
    RUN_TEST(scenario_write_back_vs_through);

    printf("\n[6] Parameter Sweep Tests\n");
    RUN_TEST(param_sweep_set_count);
    RUN_TEST(param_sweep_associativity);
    RUN_TEST(param_sweep_line_size);
    RUN_TEST(param_sweep_replacement_policy);

    printf("\n[7] Statistics Tests\n");
    RUN_TEST(stats_accuracy);

    printf("\n========== Results: %d/%d tests passed ==========\n",
           tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
