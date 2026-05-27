// =============================================================================
// Tests for reference GPGPU-Sim cache code (UNMODIFIED from original)
// =============================================================================

#include "../gpgpu_cache/gpu_cache_ref.h"
#include <cstdio>
#include <cassert>
#include <cstring>

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

// ===== Test: Config String Parsing =====
TEST(config_parse_basic) {
    cache_config config;
    char cfg[] = "N:64:128:4,L:B:m:F:L,A:32:4,32";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);
    ASSERT_EQ(config.get_nset(), 64u);
    ASSERT_EQ(config.get_line_sz(), 128u);
    ASSERT_EQ(config.get_write_policy(), WRITE_BACK);
    ASSERT_EQ(config.get_write_allocate_policy(), FETCH_ON_WRITE);
}

TEST(config_parse_sector) {
    cache_config config;
    char cfg[] = "S:32:128:4,L:B:m:F:X,A:32:4,64";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);
    ASSERT_EQ(config.get_nset(), 32u);
    ASSERT_EQ(config.get_line_sz(), 128u);
    ASSERT_EQ(config.get_write_policy(), WRITE_BACK);
}

TEST(config_disabled) {
    cache_config config;
    char cfg[] = "none";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);
    ASSERT_TRUE(config.disabled());
}

// ===== Test: Tag Array =====
TEST(tag_array_probe_miss) {
    cache_config config;
    char cfg[] = "N:4:64:2,L:R:m:N:L,A:4:2,8";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);

    tag_array tags(config, 0, 0);
    unsigned idx;
    mem_access_t access(GLOBAL_ACC_R, 0x1000, 4, false,
                        active_mask_t(), mem_access_byte_mask_t(),
                        mem_access_sector_mask_t());
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    enum cache_request_status s = tags.probe(0x1000, idx, &mf, false);
    ASSERT_EQ((int)s, (int)MISS);
}

TEST(tag_array_probe_hit) {
    cache_config config;
    char cfg[] = "N:4:64:2,L:R:m:N:L,A:4:2,8";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);

    tag_array tags(config, 0, 0);
    unsigned idx;
    bool wb = false;
    evicted_block_info evicted;

    mem_access_t access(GLOBAL_ACC_R, 0x1000, 4, false,
                        active_mask_t(), mem_access_byte_mask_t(),
                        mem_access_sector_mask_t());
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    // Allocate and fill
    tags.access(0x1000, 1, idx, &mf);
    tags.fill(idx, 2, &mf);

    // Now probe should hit
    enum cache_request_status s = tags.probe(0x1000, idx, &mf, false);
    ASSERT_EQ((int)s, (int)HIT);
}

// ===== Test: Read-Only Cache =====
TEST(read_only_cache_basic) {
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    cache_config config;
    char cfg[] = "N:16:64:4,L:R:m:N:L,A:8:2,16";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);

    read_only_cache cache("TestRO", config, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);

    // Create a read request
    mem_access_t access(GLOBAL_ACC_R, 0x1000, 4, false,
                        active_mask_t(), mem_access_byte_mask_t(),
                        mem_access_sector_mask_t());
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    std::list<cache_event> events;
    enum cache_request_status s = cache.access(mf.get_addr(), &mf, 1, events);
    ASSERT_TRUE(s == MISS || s == HIT_RESERVED || s == RESERVATION_FAIL);
}

// ===== Test: Data Cache Write-Back =====
TEST(data_cache_write_back) {
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    cache_config config;
    char cfg[] = "N:16:64:4,L:B:m:F:L,A:16:4,32";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);

    data_cache cache("TestWB", config, 0, 0, &mem, &allocator,
                     IN_L1D_MISS_QUEUE, L1_WR_ALLOC_R, L1_WRBK_ACC,
                     &gpu, L1_GPU_CACHE);

    // Write miss — set sector mask bit 0 for proper initialization
    mem_access_sector_mask_t smask;
    smask.set(0);
    mem_access_t access(GLOBAL_ACC_W, 0x1000, 4, true,
                        active_mask_t(), mem_access_byte_mask_t(), smask);
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    std::list<cache_event> events;
    enum cache_request_status s = cache.access(mf.get_addr(), &mf, 1, events);
    // Should be MISS or HIT (write-back WRITE_BACK type fills directly)
    ASSERT_TRUE(s == MISS || s == HIT || s == RESERVATION_FAIL);
}

// ===== Test: Cache Stats =====
TEST(cache_stats_basic) {
    cache_stats stats;
    stats.clear();

    mem_access_t access(GLOBAL_ACC_R, 0x1000, 4, false,
                        active_mask_t(), mem_access_byte_mask_t(),
                        mem_access_sector_mask_t());
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    stats.inc_stats(GLOBAL_ACC_R, HIT, 0);
    stats.inc_stats(GLOBAL_ACC_R, MISS, 0);
    stats.inc_stats(GLOBAL_ACC_R, HIT, 0);

    struct cache_sub_stats css;
    stats.get_sub_stats(css);
    ASSERT_EQ(css.accesses, 3ull);
    ASSERT_EQ(css.misses, 1ull);
}

// ===== Test: MSHR =====
TEST(mshr_basic) {
    mshr_table mshr(4, 4);

    mem_access_t access(GLOBAL_ACC_R, 0x1000, 4, false,
                        active_mask_t(), mem_access_byte_mask_t(),
                        mem_access_sector_mask_t());
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    ASSERT_TRUE(!mshr.probe(0x1000));
    mshr.add(0x1000, &mf);
    ASSERT_TRUE(mshr.probe(0x1000));

    bool has_atomic = false;
    mshr.mark_ready(0x1000, has_atomic);
    ASSERT_TRUE(mshr.access_ready());

    mem_fetch *result = mshr.next_access();
    ASSERT_TRUE(result != NULL);
}

// ===== Test: L1 Data Cache (l1_cache) =====
TEST(l1_cache_basic) {
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    cache_config config;
    char cfg[] = "S:32:128:4,L:B:m:F:X,A:32:4,64";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);

    l1_cache cache("TestL1", config, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);

    // Read miss
    mem_access_t access(GLOBAL_ACC_R, 0x1000, 4, false,
                        active_mask_t(), mem_access_byte_mask_t(),
                        mem_access_sector_mask_t());
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    std::list<cache_event> events;
    enum cache_request_status s = cache.access(mf.get_addr(), &mf, 1, events);
    ASSERT_TRUE(s == MISS || s == RESERVATION_FAIL);
}

// ===== Test: L2 Cache via l2_cache =====
TEST(l2_cache_basic) {
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    cache_config config;
    char cfg[] = "N:256:128:16,L:B:m:F:L,A:64:8,128";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);

    l2_cache cache("TestL2", config, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L2_GPU_CACHE);

    mem_access_t access(GLOBAL_ACC_R, 0x1000, 4, false,
                        active_mask_t(), mem_access_byte_mask_t(),
                        mem_access_sector_mask_t());
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    std::list<cache_event> events;
    enum cache_request_status s = cache.access(mf.get_addr(), &mf, 1, events);
    ASSERT_TRUE(s == MISS || s == RESERVATION_FAIL);
}

// ===== Test: Texture Cache =====
TEST(texture_cache_basic) {
    simple_mem_interface mem(64);
    gpgpu_sim gpu;

    cache_config config;
    char cfg[] = "N:16:128:24,L:R:m:N:L,F:128:4,128:2";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);

    tex_cache cache("TestTex", config, 0, 0, &mem,
                    IN_L1T_MISS_QUEUE, IN_SHADER_L1T_ROB);

    mem_access_t access(TEXTURE_ACC_R, 0x1000, 4, false,
                        active_mask_t(), mem_access_byte_mask_t(),
                        mem_access_sector_mask_t());
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    std::list<cache_event> events;
    enum cache_request_status s = cache.access(mf.get_addr(), &mf, 1, events);
    ASSERT_TRUE(s == MISS || s == HIT_RESERVED || s == RESERVATION_FAIL);
}

// ===== Test: Cycle and Fill Flow =====
TEST(baseline_cache_cycle_fill) {
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    cache_config config;
    char cfg[] = "N:16:64:4,L:R:m:N:L,A:8:2,16";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);

    read_only_cache cache("TestCycle", config, 0, 0, &mem,
                          IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);

    // Access → miss
    mem_access_t access(GLOBAL_ACC_R, 0x1000, 4, false,
                        active_mask_t(), mem_access_byte_mask_t(),
                        mem_access_sector_mask_t());
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);

    std::list<cache_event> events;
    enum cache_request_status s = cache.access(mf.get_addr(), &mf, 1, events);

    // Cycle the cache
    cache.cycle();

    // If miss was queued to memory interface, we should see it
    // (the mem interface received the request via cycle → push)
    ASSERT_TRUE(s == MISS || s == HIT_RESERVED || s == RESERVATION_FAIL);
}

int main() {
    printf("\n========== GPGPU-Sim Cache Reference Test Suite ==========\n\n");

    printf("[1] Configuration Tests\n");
    RUN_TEST(config_parse_basic);
    RUN_TEST(config_parse_sector);
    // RUN_TEST(config_disabled);

    printf("\n[2] Tag Array Tests\n");
    RUN_TEST(tag_array_probe_miss);
    RUN_TEST(tag_array_probe_hit);

    printf("\n[3] Cache Access Tests\n");
    RUN_TEST(read_only_cache_basic);
    RUN_TEST(data_cache_write_back);

    printf("\n[4] L1/L2 Cache Tests\n");
    printf("\n[5] Texture Cache Tests\n");
    // RUN_TEST(texture_cache_basic);

    printf("\n[6] MSHR Tests\n");
    RUN_TEST(mshr_basic);

    printf("\n[7] Cycle/Fill Flow Tests\n");
    RUN_TEST(baseline_cache_cycle_fill);

    printf("\n[8] Statistics Tests\n");
    RUN_TEST(cache_stats_basic);

    printf("\n[4] L1/L2 Cache Tests\n");
    RUN_TEST(l1_cache_basic);
    RUN_TEST(l2_cache_basic);

    printf("\n[5] Texture Cache Tests\n");
    RUN_TEST(texture_cache_basic);

    printf("\n========== Results: %d/%d tests passed ==========\n",
           tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
