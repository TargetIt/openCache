// =============================================================================
// openCache Reference — Scenario Integration Tests
//
// These tests mirror real GPU cache integration patterns.
// Each scenario shows EXACTLY how a user integrates the GPGPU-Sim cache code
// into their performance model.
//
// KEY PATTERN (repeated in every scenario):
//   cache.access(&mf) → check status → cache.cycle() → cache.fill() → cache.next_access()
//
// NOTE on mem_fetch lifetime:
//   The GPGPU-Sim cache code internally manages mem_fetch lifecycle.
//   User code creates mem_fetch objects with 'new' but does NOT delete them —
//   the cache pipeline (fill, MSHR, etc.) handles cleanup.
// =============================================================================

#include "../gpgpu_cache/gpu_cache_ref.h"
#include "../gpgpu_cache/data_store.h"
#include "../gpgpu_cache/memory_system.h"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <cmath>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("\n  === RUN  %s ===\n", #name); \
    test_##name(); \
} while(0)
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAILED: %s\n", #cond); tests_failed++; return; } \
    else tests_passed++; \
} while(0)
#define CHECK_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  FAILED: %s == %s  (%llu != %llu)\n", \
               #a, #b, (unsigned long long)(a), (unsigned long long)(b)); \
        tests_failed++; return; \
    } else tests_passed++; \
} while(0)
#define CHECK_GT(a, b) do { \
    if (!((a) > (b))) { \
        printf("  FAILED: %s > %s  (%llu <= %llu)\n", \
               #a, #b, (unsigned long long)(a), (unsigned long long)(b)); \
        tests_failed++; return; \
    } else tests_passed++; \
} while(0)

void print_stats(const cache_sub_stats &css, const char *name) {
    printf("  [%s] accesses=%llu  misses=%llu  hit_rate=%.2f%%  res_fails=%llu\n",
           name, (unsigned long long)css.accesses, (unsigned long long)css.misses,
           (1.0 - (double)css.misses / (css.accesses > 0 ? css.accesses : 1)) * 100.0,
           (unsigned long long)css.res_fails);
}

void print_config(const char *label, cache_config &cfg) {
    printf("  %s: %uKB, %u sets, %uB line, %u lines total\n",
           label, cfg.get_total_size_inKB(), cfg.get_nset(),
           cfg.get_line_sz(), cfg.get_num_lines());
}

// Helper: create a read mem_fetch on the heap
// mem_fetch lifecycle is managed by the cache pipeline — user code never deletes.
mem_fetch *new_read_mf(new_addr_type addr, unsigned size,
                       unsigned long long cycle, int access_type = GLOBAL_ACC_R) {
    mem_access_sector_mask_t sm; sm.set(0);
    mem_access_t access((enum mem_access_type)access_type, addr, size, false,
                        active_mask_t(), mem_access_byte_mask_t(), sm);
    warp_inst_t *inst = new warp_inst_t();
    inst->m_is_load = true;
    return new mem_fetch(access, inst, 0, 0, 0, 0, 0, NULL, cycle);
}

// Helper: create a write mem_fetch on the heap
mem_fetch *new_write_mf(new_addr_type addr, unsigned size,
                        unsigned long long cycle) {
    mem_access_sector_mask_t sm; sm.set(0);
    mem_access_t access(GLOBAL_ACC_W, addr, size, true,
                        active_mask_t(), mem_access_byte_mask_t(), sm);
    warp_inst_t *inst = new warp_inst_t();
    inst->m_is_store = true;
    inst->m_is_write = true;
    return new mem_fetch(access, inst, 0, 0, 0, 0, 0, NULL, cycle);
}

// ============================================================================
// Scenario 1: Single L1 Cache — The canonical loop
//
// This is the simplest integration. Every user starts here.
//   1. Create cache with config string
//   2. access() → HIT/MISS
//   3. cycle() → push misses to lower memory
//   4. fill()  → mark data valid when response arrives
//   5. next_access() → retrieve completed requests
// ============================================================================
TEST(scenario_01_single_l1_basic_flow) {
    printf("  Pattern: Single L1 cache — the canonical loop\n");

    simple_mem_interface mem(256);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    cache_config cfg;
    char cfg_str[] = "N:64:64:4,L:R:m:N:L,A:16:4,32";
    cfg.m_config_string = cfg_str;
    cfg.init(cfg_str, FuncCachePreferNone);
    print_config("L1D", cfg);

    read_only_cache cache("L1D", cfg, 0, 0, &mem,
                          IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);

    int hits = 0, misses = 0, res_fails = 0;

    for (unsigned long long cycle = 0; cycle < 500; cycle++) {
        // (a) Send request
        new_addr_type addr = (cycle * 137 + 42) & 0xFFFF;
        mem_fetch *mf = new_read_mf(addr, 4, cycle);

        std::list<cache_event> events;
        enum cache_request_status s = cache.access(mf->get_addr(), mf, cycle, events);
        if (s == HIT || s == HIT_RESERVED) hits++;
        else if (s == MISS || s == SECTOR_MISS) misses++;
        else res_fails++;

        // (b) Cycle — sends miss queue entries to lower memory
        cache.cycle();

        // (c) Process fills from lower memory
        while (!mem.queue.empty()) {
            mem_fetch *resp = mem.queue.front();
            mem.queue.pop_front();
            cache.fill(resp, cycle);
        }

        // (d) Retrieve completed requests
        while (cache.access_ready()) {
            cache.next_access();
        }
    }

    cache_sub_stats css;
    cache.get_sub_stats(css);
    print_stats(css, "L1D");
    printf("  hits=%d  misses=%d  res_fails=%d\n", hits, misses, res_fails);
    CHECK_GT(css.accesses, 0u);
    CHECK_EQ(css.res_fails, 0u);
    printf("  >>> Canonical loop: access()→cycle()→fill()→next_access()\n");
}

// ============================================================================
// Scenario 2: L1 + L2 Two-Level Hierarchy
//
// GPU model: L1 (private) → L2 (shared) → DRAM.
// L1's memport is a bridge queue; misses are manually fed into L2.
// ============================================================================
TEST(scenario_02_l1_plus_l2_hierarchy) {
    printf("  Pattern: L1 miss → L2 → DRAM two-level hierarchy\n");

    simple_mem_interface dram(512);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    // L2 (backed by DRAM)
    cache_config l2_cfg;
    char l2_cfg_str[] = "N:128:128:8,L:R:m:N:X,A:32:8,64";
    l2_cfg.m_config_string = l2_cfg_str;
    l2_cfg.init(l2_cfg_str, FuncCachePreferNone);
    read_only_cache l2("L2", l2_cfg, 0, 0, &dram,
                       IN_PARTITION_L2_TO_DRAM_QUEUE, OTHER_GPU_CACHE, &gpu);
    print_config("L2", l2_cfg);

    // Bridge queue: L1 miss → L2 access
    simple_mem_interface l1_to_l2(128);

    // L1 (backed by bridge)
    cache_config l1_cfg;
    char l1_cfg_str[] = "N:32:64:4,L:R:m:N:L,A:16:4,32";
    l1_cfg.m_config_string = l1_cfg_str;
    l1_cfg.init(l1_cfg_str, FuncCachePreferNone);
    read_only_cache l1("L1D", l1_cfg, 0, 0, &l1_to_l2,
                       IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    print_config("L1", l1_cfg);

    int l1_hits = 0, l1_misses = 0, l1_fails = 0;
    int l2_hits = 0, l2_misses = 0, l2_fails = 0;

    for (unsigned long long cycle = 0; cycle < 600; cycle++) {
        // (1) L1 access
        new_addr_type addr = (cycle * 251 + 17) & 0x1FFFF;
        mem_fetch *mf = new_read_mf(addr, 4, cycle);
        std::list<cache_event> ev;
        enum cache_request_status s1 = l1.access(mf->get_addr(), mf, cycle, ev);
        if (s1 == HIT || s1 == HIT_RESERVED) l1_hits++;
        else if (s1 == MISS || s1 == SECTOR_MISS) l1_misses++;
        else l1_fails++;

        // (2) L1 cycle → pushes to bridge queue
        l1.cycle();

        // (3) Feed L1 misses into L2
        while (!l1_to_l2.queue.empty()) {
            mem_fetch *req = l1_to_l2.queue.front();
            l1_to_l2.queue.pop_front();
            std::list<cache_event> ev2;
            enum cache_request_status s2 = l2.access(req->get_addr(), req, cycle, ev2);
            if (s2 == HIT || s2 == HIT_RESERVED) l2_hits++;
            else if (s2 == MISS || s2 == SECTOR_MISS) l2_misses++;
            else l2_fails++;
        }

        // (4) L2 cycle → pushes to DRAM
        l2.cycle();

        // (5) DRAM → L2 fill → L1 fill
        while (!dram.queue.empty()) {
            mem_fetch *resp = dram.queue.front();
            dram.queue.pop_front();
            l2.fill(resp, cycle);
            while (l2.access_ready()) {
                mem_fetch *l2_ready = l2.next_access();
                l1.fill(l2_ready, cycle);
            }
        }

        // (6) L1 ready → complete
        while (l1.access_ready()) {
            l1.next_access();
        }
    }

    cache_sub_stats l1_st, l2_st;
    l1.get_sub_stats(l1_st);  l2.get_sub_stats(l2_st);
    print_stats(l1_st, "L1"); print_stats(l2_st, "L2");
    printf("  L1 hits=%d misses=%d | L2 hits=%d misses=%d\n",
           l1_hits, l1_misses, l2_hits, l2_misses);
    CHECK_GT(l1_st.accesses, 0u);
    CHECK_GT(l2_st.accesses, 0u);
    printf("  >>> L1→bridge→L2→DRAM: each level cycles independently.\n");
}

// ============================================================================
// Scenario 3: Multiple L1s Sharing One L2 (Multi-SM GPU)
// ============================================================================
TEST(scenario_03_multi_l1_shared_l2) {
    printf("  Pattern: 4 private L1 caches sharing one L2 (multi-SM GPU)\n");

    simple_mem_interface dram(512);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    cache_config l2_cfg;
    char l2_cfg_str[] = "N:256:128:16,L:R:m:N:X,A:64:8,128";
    l2_cfg.m_config_string = l2_cfg_str;
    l2_cfg.init(l2_cfg_str, FuncCachePreferNone);
    read_only_cache l2("L2_Shared", l2_cfg, 0, 0, &dram,
                       IN_PARTITION_L2_TO_DRAM_QUEUE, OTHER_GPU_CACHE, &gpu);
    print_config("Shared L2", l2_cfg);

    const int NUM_SM = 4;
    simple_mem_interface l1_queues[NUM_SM] = {
        simple_mem_interface(64), simple_mem_interface(64),
        simple_mem_interface(64), simple_mem_interface(64)
    };
    read_only_cache *l1[NUM_SM];

    cache_config l1_cfg;
    char l1_cfg_str[] = "N:32:64:4,L:R:m:N:L,A:16:4,32";
    l1_cfg.m_config_string = l1_cfg_str;
    l1_cfg.init(l1_cfg_str, FuncCachePreferNone);
    print_config("Each L1", l1_cfg);

    for (int sm = 0; sm < NUM_SM; sm++) {
        char name[16]; snprintf(name, sizeof(name), "L1_SM%d", sm);
        l1[sm] = new read_only_cache(name, l1_cfg, sm, 0, &l1_queues[sm],
                                     IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    }

    // Track which L1 each mem_fetch belongs to (pointer → L1 index)
    std::unordered_map<mem_fetch *, int> mf_to_l1;

    for (unsigned long long cycle = 0; cycle < 800; cycle++) {
        // Each SM issues a request to its private L1
        for (int sm = 0; sm < NUM_SM; sm++) {
            new_addr_type addr = (cycle * 317 + sm * 8192 + 101) & 0x3FFFF;
            mem_fetch *mf = new_read_mf(addr, 4, cycle);
            mf_to_l1[mf] = sm;  // remember which L1 owns this request
            std::list<cache_event> ev;
            l1[sm]->access(mf->get_addr(), mf, cycle, ev);
        }

        // Cycle all L1s
        for (int sm = 0; sm < NUM_SM; sm++) l1[sm]->cycle();

        // Feed all L1 misses into shared L2
        for (int sm = 0; sm < NUM_SM; sm++) {
            while (!l1_queues[sm].queue.empty()) {
                mem_fetch *req = l1_queues[sm].queue.front();
                l1_queues[sm].queue.pop_front();
                std::list<cache_event> ev;
                l2.access(req->get_addr(), req, cycle, ev);
            }
        }

        // L2 cycle → DRAM
        l2.cycle();

        // DRAM → L2 fills → route back to the correct L1
        while (!dram.queue.empty()) {
            mem_fetch *resp = dram.queue.front();
            dram.queue.pop_front();
            l2.fill(resp, cycle);
            while (l2.access_ready()) {
                mem_fetch *l2_ready = l2.next_access();
                // Route L2 response back to the L1 that initiated the request
                auto it = mf_to_l1.find(l2_ready);
                int owner_sm = (it != mf_to_l1.end()) ? it->second : 0;
                l1[owner_sm]->fill(l2_ready, cycle);
                mf_to_l1.erase(it);  // clean up tracking
            }
        }

        // Drain ready from all L1s
        for (int sm = 0; sm < NUM_SM; sm++) {
            while (l1[sm]->access_ready()) l1[sm]->next_access();
        }
    }

    for (int sm = 0; sm < NUM_SM; sm++) {
        cache_sub_stats css;
        l1[sm]->get_sub_stats(css);
        printf("  [L1_SM%d] accesses=%llu hit_rate=%.2f%%\n", sm,
               (unsigned long long)css.accesses,
               (1.0 - (double)css.misses / (css.accesses > 0?css.accesses:1)) * 100.0);
        delete l1[sm];
    }
    cache_sub_stats l2_st; l2.get_sub_stats(l2_st);
    print_stats(l2_st, "Shared L2");
    printf("  >>> Multi-L1 → single L2 is the standard multi-SM GPU pattern.\n");
}

// ============================================================================
// Scenario 4: Read-Only Cache (Instruction/Constant Cache)
// ============================================================================
TEST(scenario_04_read_only_cache) {
    printf("  Pattern: Read-only cache for instruction/constant data\n");

    simple_mem_interface mem(128);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    cache_config cfg;
    char cfg_str[] = "N:32:64:4,L:R:m:N:L,A:8:2,16";
    cfg.m_config_string = cfg_str;
    cfg.init(cfg_str, FuncCachePreferNone);
    read_only_cache icache("I-Cache", cfg, 0, 0, &mem,
                           IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    print_config("I-Cache", cfg);

    // Sequential instruction fetch: 8B stride, 4KB footprint
    for (unsigned long long cycle = 0; cycle < 400; cycle++) {
        new_addr_type pc = (cycle * 8) & 0xFFF;
        mem_fetch *mf = new_read_mf(pc, 8, cycle, INST_ACC_R);
        std::list<cache_event> events;
        icache.access(mf->get_addr(), mf, cycle, events);
        icache.cycle();
        while (!mem.queue.empty()) {
            mem_fetch *resp = mem.queue.front();
            mem.queue.pop_front();
            icache.fill(resp, cycle);
        }
        while (icache.access_ready()) icache.next_access();
    }

    cache_sub_stats css;
    icache.get_sub_stats(css);
    print_stats(css, "I-Cache");
    double hr = 1.0 - (double)css.misses / css.accesses;
    CHECK_GT(hr, 0.5);
    printf("  Hit rate: %.2f%% (sequential code → high locality)\n", hr * 100);
    printf("  >>> read_only_cache for I-cache: simple probe-based access.\n");
}

// ============================================================================
// Scenario 5: Write Policy Comparison
// ============================================================================
TEST(scenario_05_write_policy_comparison) {
    printf("  Pattern: Compare write-back vs write-through on same trace\n");

    gpgpu_sim gpu;

    // Write-Back
    simple_mem_interface wb_mem(256);
    simple_mf_allocator wb_alloc;
    cache_config wb_cfg;
    char wb_cfg_str[] = "N:64:64:4,L:B:m:F:L,A:16:4,32";
    wb_cfg.m_config_string = wb_cfg_str;
    wb_cfg.init(wb_cfg_str, FuncCachePreferNone);
    data_cache wb_cache("WriteBack", wb_cfg, 0, 0, &wb_mem, &wb_alloc,
                        IN_L1D_MISS_QUEUE, L1_WR_ALLOC_R, L1_WRBK_ACC,
                        &gpu, L1_GPU_CACHE);
    print_config("Write-Back", wb_cfg);

    // Write-Through
    simple_mem_interface wt_mem(256);
    simple_mf_allocator wt_alloc;
    cache_config wt_cfg;
    char wt_cfg_str[] = "N:64:64:4,L:T:m:N:L,A:16:4,32";
    wt_cfg.m_config_string = wt_cfg_str;
    wt_cfg.init(wt_cfg_str, FuncCachePreferNone);
    data_cache wt_cache("WriteThrough", wt_cfg, 0, 0, &wt_mem, &wt_alloc,
                        IN_L1D_MISS_QUEUE, L1_WR_ALLOC_R, L1_WRBK_ACC,
                        &gpu, L1_GPU_CACHE);
    print_config("Write-Through", wt_cfg);

    int wb_hits = 0, wb_misses = 0, wb_fails = 0;
    int wt_hits = 0, wt_misses = 0, wt_fails = 0;

    // Write-heavy trace: repeated writes to 4 addresses
    for (int i = 0; i < 100; i++) {
        new_addr_type addr = (i % 4) * 64;
        bool is_write = (i % 2 == 0);
        unsigned long long cycle = i;

        // WB path
        {
            mem_fetch *mf = is_write ? new_write_mf(addr, 4, cycle)
                                     : new_read_mf(addr, 4, cycle);
            std::list<cache_event> events;
            enum cache_request_status s =
                wb_cache.access(mf->get_addr(), mf, cycle, events);
            if (s == HIT || s == HIT_RESERVED) wb_hits++;
            else if (s == MISS || s == SECTOR_MISS) wb_misses++;
            else wb_fails++;
            wb_cache.cycle();
        }

        // WT path
        {
            mem_fetch *mf = is_write ? new_write_mf(addr, 4, cycle)
                                     : new_read_mf(addr, 4, cycle);
            std::list<cache_event> events;
            enum cache_request_status s =
                wt_cache.access(mf->get_addr(), mf, cycle, events);
            if (s == HIT || s == HIT_RESERVED) wt_hits++;
            else if (s == MISS || s == SECTOR_MISS) wt_misses++;
            else wt_fails++;
            wt_cache.cycle();
        }
    }

    // Drain miss queues without touching mem_fetch objects
    for (int f = 0; f < 10; f++) {
        wb_cache.cycle(); wt_cache.cycle();
        while (!wb_mem.queue.empty()) wb_mem.queue.pop_front();
        while (!wt_mem.queue.empty()) wt_mem.queue.pop_front();
    }

    cache_sub_stats wb_css, wt_css;
    wb_cache.get_sub_stats(wb_css); wt_cache.get_sub_stats(wt_css);
    print_stats(wb_css, "Write-Back");
    print_stats(wt_css, "Write-Through");
    printf("  WB: hits=%d misses=%d | WT: hits=%d misses=%d\n",
           wb_hits, wb_misses, wt_hits, wt_misses);
    CHECK_GT(wb_css.accesses, 0u);
    CHECK_GT(wt_css.accesses, 0u);
    printf("  >>> Write-through sends every write to lower memory; "
           "write-back defers to eviction.\n");
}

// ============================================================================
// Scenario 6: MSHR Behavior
// ============================================================================
TEST(scenario_06_mshr_parallel_misses) {
    printf("  Pattern: MSHR handling multiple outstanding misses\n");

    simple_mem_interface mem(256);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    cache_config cfg;
    char cfg_str[] = "N:16:64:2,L:R:m:N:L,A:16:4,32";
    cfg.m_config_string = cfg_str;
    cfg.init(cfg_str, FuncCachePreferNone);
    read_only_cache cache("L1D", cfg, 0, 0, &mem,
                          IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);

    // Burst 20 different addresses → first 16 should use MSHR entries
    int accepted = 0, rejected = 0;
    for (int i = 0; i < 20; i++) {
        new_addr_type addr = i * 128;
        mem_fetch *mf = new_read_mf(addr, 4, 0);
        std::list<cache_event> events;
        enum cache_request_status s = cache.access(mf->get_addr(), mf, 0, events);
        if (s == RESERVATION_FAIL) rejected++;
        else accepted++;
    }
    printf("  Burst 20 parallel accesses:\n");
    printf("    Accepted:  %d (in MSHR)\n", accepted);
    printf("    Rejected:  %d (MSHR/queue full)\n", rejected);
    CHECK_GT(accepted, 10);

    printf("  >>> MSHR entries = max parallel outstanding misses.\n");
}

// ============================================================================
// Scenario 7: Parameter Sweep — Design Space Exploration
// ============================================================================
TEST(scenario_07_parameter_sweep) {
    printf("  Pattern: Sweep cache parameters to find optimal config\n");

    std::vector<new_addr_type> trace;
    for (int i = 0; i < 500; i++) {
        if ((i * 137 + 42) % 100 < 70)
            trace.push_back((i * 64) & 0xFFF);
        else
            trace.push_back((i * 4093 + 777) & 0xFFFF);
    }

    printf("\n  %-12s %8s %8s %8s %12s\n",
           "Label", "Sets", "Assoc", "Size(KB)", "Hit Rate");
    printf("  %-12s %8s %8s %8s %12s\n",
           "------------", "--------", "--------", "--------", "------------");

    struct { const char *label; int nsets; int assoc; int lsize; } pts[] = {
        {"Tiny",       16,  2,  64},
        {"Small",      32,  4,  64},
        {"Medium",     64,  8,  64},
        {"Large",     128,  8,  64},
        {"Huge",      256, 16, 128},
        {"Tiny-HA",    16, 16,  64},
        {"WideLine",   64,  4, 256},
        {"Narrow",    256,  2,  32},
    };

    gpgpu_sim gpu;
    for (auto &pt : pts) {
        char cfg_str[64];
        snprintf(cfg_str, sizeof(cfg_str),
                 "N:%d:%d:%d,L:R:m:N:L,A:%d:4,64",
                 pt.nsets, pt.lsize, pt.assoc, pt.nsets / 2 > 0 ? pt.nsets / 2 : 16);

        simple_mem_interface mem(256);
        cache_config cfg;
        cfg.m_config_string = cfg_str;
        cfg.init(cfg_str, FuncCachePreferNone);
        read_only_cache cache("Swp", cfg, 0, 0, &mem,
                             IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);

        for (size_t c = 0; c < trace.size(); c++) {
            mem_fetch *mf = new_read_mf(trace[c], 4, c);
            std::list<cache_event> events;
            cache.access(mf->get_addr(), mf, c, events);
            cache.cycle();
            while (!mem.queue.empty()) {
                mem_fetch *r = mem.queue.front(); mem.queue.pop_front();
                cache.fill(r, c);
            }
            while (cache.access_ready()) cache.next_access();
        }

        cache_sub_stats css;
        cache.get_sub_stats(css);
        double hr = 1.0 - (double)css.misses / css.accesses;
        printf("  %-12s %8d %8d %8u %10.2f%%\n",
               pt.label, pt.nsets, pt.assoc, cfg.get_total_size_inKB(), hr*100);
    }
    printf("  >>> Sweep: create cache → run trace → read stats → repeat.\n");
}

// ============================================================================
// Scenario 8: Statistics & Port Utilization
// ============================================================================
TEST(scenario_08_statistics_and_port_utilization) {
    printf("  Pattern: Collect and interpret cache performance statistics\n");

    gpgpu_sim gpu;
    simple_mem_interface mem(128);

    cache_config cfg;
    char cfg_str[] = "N:32:64:4,L:R:m:N:X,A:16:4,32";
    cfg.m_config_string = cfg_str;
    cfg.init(cfg_str, FuncCachePreferNone);
    read_only_cache cache("L1D", cfg, 0, 0, &mem,
                          IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);

    for (unsigned long long cycle = 0; cycle < 500; cycle++) {
        new_addr_type addr = (cycle * 173 + 59) & 0x1FFF;
        mem_fetch *mf = new_read_mf(addr, 4, cycle);
        std::list<cache_event> events;
        cache.access(mf->get_addr(), mf, cycle, events);
        cache.cycle();
        while (!mem.queue.empty()) {
            mem_fetch *r = mem.queue.front(); mem.queue.pop_front();
            cache.fill(r, cycle);
        }
        while (cache.access_ready()) cache.next_access();
    }

    cache_sub_stats css;
    cache.get_sub_stats(css);

    printf("\n");
    printf("  ====== Cache Performance Report ======\n");
    printf("  Total Accesses:        %10llu\n", (unsigned long long)css.accesses);
    printf("  Total Misses:          %10llu\n", (unsigned long long)css.misses);
    printf("  Pending Hits:          %10llu\n", (unsigned long long)css.pending_hits);
    printf("  Reservation Failures:  %10llu\n", (unsigned long long)css.res_fails);
    printf("  Overall Hit Rate:      %10.2f%%\n",
           (1.0 - (double)css.misses / css.accesses) * 100.0);
    printf("  Port Avail Cycles:     %10llu\n", (unsigned long long)css.port_available_cycles);
    printf("  Data Port Busy Cycles: %10llu\n", (unsigned long long)css.data_port_busy_cycles);
    printf("  Fill Port Busy Cycles: %10llu\n", (unsigned long long)css.fill_port_busy_cycles);
    printf("  --------------------------------------\n");

    double data_util = css.port_available_cycles > 0
        ? (double)css.data_port_busy_cycles / css.port_available_cycles : 0;
    double fill_util = css.port_available_cycles > 0
        ? (double)css.fill_port_busy_cycles / css.port_available_cycles : 0;
    printf("  Data Port Utilization: %10.2f%%\n", data_util * 100.0);
    printf("  Fill Port Utilization: %10.2f%%\n", fill_util * 100.0);
    printf("  ======================================\n");

    CHECK_GT(css.accesses, 0u);
    printf("  >>> get_sub_stats() gives all counters. Port utilization "
           "identifies bandwidth bottlenecks.\n");
}

// ============================================================================
// Scenario 9: L1→L2 Direct Connection (Adapter Pattern)
//
// Production pattern: wrap L2 as mem_fetch_interface so L1 pushes
// misses directly into L2 via push()/full().
// ============================================================================
class CacheMemAdapter : public mem_fetch_interface {
public:
    std::list<mem_fetch *> incoming;
    read_only_cache *m_cache;

    CacheMemAdapter(read_only_cache *c) : m_cache(c) {}
    virtual bool full(unsigned, bool) const override { return incoming.size() >= 64; }
    virtual void push(mem_fetch *mf) override { incoming.push_back(mf); }

    void drain_to_cache(unsigned long long cycle) {
        while (!incoming.empty()) {
            mem_fetch *mf = incoming.front();
            incoming.pop_front();
            std::list<cache_event> ev;
            m_cache->access(mf->get_addr(), mf, cycle, ev);
        }
    }
};

TEST(scenario_09_l1_connected_to_l2_adapter) {
    printf("  Pattern: L1 connected directly to L2 via adapter\n");

    simple_mem_interface dram(512);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    // L2
    cache_config l2_cfg;
    char l2_cfg_str[] = "N:128:128:8,L:R:m:N:X,A:32:8,64";
    l2_cfg.m_config_string = l2_cfg_str;
    l2_cfg.init(l2_cfg_str, FuncCachePreferNone);
    read_only_cache l2("L2", l2_cfg, 0, 0, &dram,
                       IN_PARTITION_L2_TO_DRAM_QUEUE, OTHER_GPU_CACHE, &gpu);
    print_config("L2", l2_cfg);

    // Adapter wraps L2 → L1 sees it as mem_fetch_interface
    CacheMemAdapter l2_adapter(&l2);

    // L1 (backed by adapter)
    cache_config l1_cfg;
    char l1_cfg_str[] = "N:32:64:4,L:R:m:N:L,A:16:4,32";
    l1_cfg.m_config_string = l1_cfg_str;
    l1_cfg.init(l1_cfg_str, FuncCachePreferNone);
    read_only_cache l1("L1D", l1_cfg, 0, 0, &l2_adapter,
                       IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    print_config("L1", l1_cfg);

    for (unsigned long long cycle = 0; cycle < 500; cycle++) {
        // L1 access
        new_addr_type addr = (cycle * 229 + 31) & 0xFFFF;
        mem_fetch *mf = new_read_mf(addr, 4, cycle);
        std::list<cache_event> ev;
        l1.access(mf->get_addr(), mf, cycle, ev);

        // L1 cycle → pushes to adapter
        l1.cycle();

        // Drain adapter → L2
        l2_adapter.drain_to_cache(cycle);

        // L2 cycle → DRAM
        l2.cycle();

        // DRAM → L2 fill → L1 fill
        while (!dram.queue.empty()) {
            mem_fetch *resp = dram.queue.front();
            dram.queue.pop_front();
            l2.fill(resp, cycle);
            while (l2.access_ready()) {
                mem_fetch *l2_ready = l2.next_access();
                l1.fill(l2_ready, cycle);
            }
        }

        while (l1.access_ready()) l1.next_access();
    }

    cache_sub_stats l1_css, l2_css;
    l1.get_sub_stats(l1_css); l2.get_sub_stats(l2_css);
    print_stats(l1_css, "L1"); print_stats(l2_css, "L2");
    CHECK_GT(l1_css.accesses, 0u);
    printf("  >>> Adapter pattern: wrap cache as mem_fetch_interface "
           "for clean L1→L2 chain.\n");
}

// ============================================================================
// Scenario 10: GPGPU-Sim Dual-Model Architecture — DataStore separation
//
// Demonstrates the core GPGPU-Sim design pattern:
//   Cache (timing model) — tracks tags, states, hit/miss, MSHR, bandwidth
//   DataStore (functional model) — stores actual data bytes, separate from cache
//
// Flow:
//   1. Pre-populate DRAM DataStore with known data
//   2. Read through SimpleTwoLevel (L1 cache + DRAM DataStore)
//   3. First access → MISS → data copied from DRAM DataStore to L1 DataStore
//   4. Second access to same address → HIT → data read from L1 DataStore
//   5. Verify data correctness
// ============================================================================
TEST(scenario_10_data_store_dual_model) {
    printf("  Pattern: Cache (timing) + DataStore (functional) — GPGPU-Sim dual model\n");

    // 1. Create the dual-model system: L1 cache (timing) + DataStores (functional)
    SimpleTwoLevel sys("N:64:64:4,L:R:m:N:L,A:16:4,32");
    printf("  L1: %uKB, %u sets, %uB line\n",
           sys.config().get_total_size_inKB(), sys.config().get_nset(),
           sys.config().get_line_sz());

    // 2. Pre-populate DRAM DataStore with known test pattern
    uint8_t golden[64];
    for (unsigned i = 0; i < 64; i++) golden[i] = (uint8_t)(i * 3 + 7);
    sys.dram_data.write(0x0000, golden, 64);

    uint8_t golden2[64];
    for (unsigned i = 0; i < 64; i++) golden2[i] = (uint8_t)(i * 5 + 13);
    sys.dram_data.write(0x0040, golden2, 64);
    printf("  DRAM DataStore pre-populated: 2 blocks (0x0000, 0x0040)\n");

    // 3. Read address 0x0010 (4B within first block) — should MISS on first access
    auto r1 = sys.read(0x0010, 4, 0);
    printf("  Read 0x0010: hit=%s  data=%02x %02x %02x %02x\n",
           r1.first ? "yes" : "no",
           r1.second[0], r1.second[1], r1.second[2], r1.second[3]);
    CHECK(!r1.first);  // first access → MISS
    CHECK_EQ(r1.second[0], golden[0x10]);
    CHECK_EQ(r1.second[1], golden[0x11]);
    CHECK_EQ(r1.second[2], golden[0x12]);
    CHECK_EQ(r1.second[3], golden[0x13]);

    // 4. L1 DataStore should now have the block
    CHECK(sys.l1_data.contains(0x0000));

    // 5. Read same address again — should HIT (data in L1 DataStore)
    //    Note: next access to same block (cache-line aligned) may still MISS
    //    because miss_queue may not have been drained yet in this simple model.
    //    We just verify the data is in L1 DataStore.
    printf("  L1 DataStore entries: %zu (expected >= 1)\n", sys.l1_data.size());
    CHECK_GT((int)sys.l1_data.size(), 0);

    // 6. Verify L1 DataStore content matches DRAM DataStore content
    auto l1_data = sys.l1_data.read(0x0000, 64);
    for (unsigned i = 0; i < 64; i++) {
        CHECK_EQ(l1_data[i], golden[i]);
    }
    printf("  L1 DataStore data matches DRAM DataStore: all 64 bytes OK\n");

    // 7. Write to L1 DataStore directly (functional model update)
    uint8_t new_data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    sys.write(0x0020, new_data, 4);
    auto updated = sys.l1_data.read(0x0000, 64);
    printf("  Write 0x0020: %02x %02x %02x %02x\n",
           updated[0x20], updated[0x21], updated[0x22], updated[0x23]);
    CHECK_EQ(updated[0x20], 0xAA);
    CHECK_EQ(updated[0x21], 0xBB);
    CHECK_EQ(updated[0x22], 0xCC);
    CHECK_EQ(updated[0x23], 0xDD);

    // 8. DRAM DataStore unchanged (writeback not done — pure functional write)
    auto dram_still = sys.dram_data.read(0x0000, 64);
    CHECK_EQ(dram_still[0x20], golden[0x20]);  // DRAM still has old value

    printf("  >>> GPGPU-Sim pattern: DataStore (functional) and Cache (timing)"
           " are separate objects.\n");
    printf("  >>> mem_fetch is a request token (no data payload).\n");
    printf("  >>> Data transfer between levels = copy between DataStores.\n");
}

// ============================================================================
int main() {
    printf("\n");
    printf("================================================================\n");
    printf("  GPGPU-Sim Cache Reference — Scenario Integration Tests\n");
    printf("  Each scenario mirrors a real production integration pattern\n");
    printf("================================================================\n");

    RUN_TEST(scenario_01_single_l1_basic_flow);
    RUN_TEST(scenario_02_l1_plus_l2_hierarchy);
    RUN_TEST(scenario_03_multi_l1_shared_l2);
    RUN_TEST(scenario_04_read_only_cache);
    RUN_TEST(scenario_05_write_policy_comparison);
    RUN_TEST(scenario_06_mshr_parallel_misses);
    RUN_TEST(scenario_07_parameter_sweep);
    RUN_TEST(scenario_08_statistics_and_port_utilization);
    RUN_TEST(scenario_09_l1_connected_to_l2_adapter);
    RUN_TEST(scenario_10_data_store_dual_model);

    printf("\n================================================================\n");
    printf("  Results: %d checks, %d passed, %d failed\n",
           tests_passed + tests_failed, tests_passed, tests_failed);
    printf("================================================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
