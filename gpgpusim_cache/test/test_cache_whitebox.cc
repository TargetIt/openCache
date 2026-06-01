// =============================================================================
// Deep whitebox tests for the standalone GPGPU-Sim cache reference.
// =============================================================================

#include "../gpgpu_cache/gpu_cache_ref.h"
#include "../gpgpu_cache/data_store.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <random>
#include <vector>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static bool current_test_failed = false;

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    tests_run++; \
    current_test_failed = false; \
    printf("  RUN  %s ... ", #name); \
    test_##name(); \
    if (current_test_failed) { \
        tests_failed++; \
    } else { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while (0)

#define CHECK_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n  CHECK_TRUE(%s) at %s:%d\n", #cond, __FILE__, __LINE__); \
        current_test_failed = true; \
        return; \
    } \
} while (0)

#define CHECK_FALSE(cond) CHECK_TRUE(!(cond))

#define CHECK_EQ(a, b) do { \
    auto _a = (a); \
    auto _b = (b); \
    if (!(_a == _b)) { \
        printf("FAILED\n  CHECK_EQ(%s, %s): %llu != %llu at %s:%d\n", \
               #a, #b, (unsigned long long)_a, (unsigned long long)_b, \
               __FILE__, __LINE__); \
        current_test_failed = true; \
        return; \
    } \
} while (0)

static cache_config make_config(const char *text)
{
    cache_config cfg;
    char *copy = strdup(text);
    cfg.m_config_string = copy;
    cfg.init(copy, FuncCachePreferNone);
    return cfg;
}

static mem_access_byte_mask_t byte_mask_range(unsigned begin, unsigned count)
{
    mem_access_byte_mask_t mask;
    for (unsigned i = begin; i < begin + count && i < MAX_MEMORY_ACCESS_SIZE; ++i)
        mask.set(i);
    return mask;
}

static mem_access_sector_mask_t sector_mask(unsigned sector)
{
    mem_access_sector_mask_t mask;
    mask.set(sector);
    return mask;
}

static mem_fetch *new_mf(new_addr_type addr, unsigned size, bool wr,
                         mem_access_type type, unsigned cycle = 0,
                         mem_access_sector_mask_t sm = sector_mask(0),
                         mem_access_byte_mask_t bm = byte_mask_range(0, MAX_MEMORY_ACCESS_SIZE))
{
    mem_access_t access(type, addr, size, wr, active_mask_t(), bm, sm);
    warp_inst_t *inst = new warp_inst_t();
    inst->m_is_load = !wr;
    inst->m_is_store = wr;
    inst->m_is_write = wr;
    return new mem_fetch(access, inst, 0, 0, 0, 0, 0, NULL, cycle);
}

static bool has_event(const std::list<cache_event> &events, cache_event_type type)
{
    for (const cache_event &event : events)
        if (event.m_cache_event_type == type)
            return true;
    return false;
}

static unsigned count_event(const std::list<cache_event> &events, cache_event_type type)
{
    unsigned count = 0;
    for (const cache_event &event : events)
        if (event.m_cache_event_type == type)
            ++count;
    return count;
}

static cache_event find_event(const std::list<cache_event> &events,
                              cache_event_type type)
{
    for (const cache_event &event : events)
        if (event.m_cache_event_type == type)
            return event;
    return cache_event(type);
}

static void drain_one_level(baseline_cache &cache, simple_mem_interface &mem,
                            unsigned cycle)
{
    cache.cycle();
    while (!mem.queue.empty()) {
        mem_fetch *resp = mem.queue.front();
        mem.queue.pop_front();
        cache.fill(resp, cycle);
    }
    while (cache.access_ready())
        cache.next_access();
}

static void fill_read_only_line(read_only_cache &cache, simple_mem_interface &mem,
                                new_addr_type addr, unsigned cycle)
{
    std::list<cache_event> events;
    mem_fetch *read = new_mf(addr, 4, false, GLOBAL_ACC_R, cycle);
    CHECK_EQ(cache.access(read->get_addr(), read, cycle, events), MISS);
    drain_one_level(cache, mem, cycle + 1);
}

TEST(config_sector_and_streaming)
{
    cache_config normal = make_config("N:4:64:2,L:R:m:N:L,A:4:2,8");
    CHECK_EQ(normal.get_atom_sz(), 64u);
    CHECK_FALSE(normal.is_streaming());

    cache_config sector = make_config("S:4:128:2,L:B:m:F:X,A:4:2,8");
    CHECK_EQ(sector.get_atom_sz(), (unsigned)SECTOR_SIZE);

    cache_config streaming = make_config("N:8:64:2,L:R:s:N:L,A:8:2,16");
    CHECK_TRUE(streaming.is_streaming());
}

TEST(address_mapping_boundaries)
{
    cache_config cfg = make_config("S:8:128:4,L:R:m:N:X,A:8:2,16");
    CHECK_EQ(cfg.block_addr(0x107f), 0x1000ull);
    CHECK_EQ(cfg.block_addr(0x1080), 0x1080ull);
    CHECK_EQ(cfg.tag(0x107f), 0x1000ull);
    CHECK_EQ(cfg.mshr_addr(0x101f), 0x1000ull);
    CHECK_EQ(cfg.mshr_addr(0x1020), 0x1020ull);
    CHECK_TRUE(cfg.set_index(0) < cfg.get_nset());
    CHECK_TRUE(cfg.set_index(~0ull) < cfg.get_nset());

    cache_config linear = make_config("N:16:64:2,L:R:m:N:L,A:8:2,16");
    cache_config xoring = make_config("N:16:64:2,L:R:m:N:X,A:8:2,16");
    cache_config ipoly = make_config("N:16:64:2,L:R:m:N:P,A:8:2,16");
    cache_config custom = make_config("N:16:64:2,L:R:m:N:C,A:8:2,16");
    cache_config fermi = make_config("N:32:64:2,L:R:m:N:H,A:8:2,16");
    CHECK_TRUE(linear.set_index(0x4000) < linear.get_nset());
    CHECK_TRUE(xoring.set_index(0x4000) < xoring.get_nset());
    CHECK_TRUE(ipoly.set_index(0x4000) < ipoly.get_nset());
    CHECK_TRUE(custom.set_index(0x4000) < custom.get_nset());
    CHECK_TRUE(fermi.set_index(0x4000) < fermi.get_nset());
    CHECK_TRUE(linear.set_index(0x1000) != xoring.set_index(0x1000) ||
               linear.set_index(0x2000) != xoring.set_index(0x2000));

    CHECK_EQ(linear.set_index(0x12340), 13u);
    CHECK_EQ(xoring.set_index(0x12340), 5u);
    CHECK_EQ(ipoly.set_index(0x12340), 9u);
    CHECK_EQ(custom.set_index(0x12340), 0u);
    CHECK_EQ(fermi.set_index(0x12340), 12u);
    CHECK_EQ(fermi.set_index(0x2000), 1u);
}

TEST(tag_fill_hit_reserved_and_sector_miss)
{
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,8");
    tag_array tags(cfg, 0, 0);
    unsigned idx = 0;
    mem_fetch *read = new_mf(0x1000, 4, false, GLOBAL_ACC_R);

    CHECK_EQ(tags.probe(0x1000, idx, read, false), MISS);
    CHECK_EQ(tags.access(0x1000, 1, idx, read), MISS);
    CHECK_EQ(tags.probe(0x1000, idx, read, false), HIT_RESERVED);
    tags.fill(idx, 2, read);
    CHECK_EQ(tags.probe(0x1000, idx, read, false), HIT);

    cache_config sector_cfg = make_config("S:4:128:2,L:R:m:N:L,S:4:2,8");
    tag_array sector_tags(sector_cfg, 0, 0);
    mem_fetch *sec0 = new_mf(0x2000, 4, false, GLOBAL_ACC_R, 0,
                             sector_mask(0), byte_mask_range(0, 32));
    CHECK_EQ(sector_tags.access(0x2000, 1, idx, sec0), MISS);
    sector_tags.fill(idx, 2, sec0);
    mem_fetch *sec1 = new_mf(0x2020, 4, false, GLOBAL_ACC_R, 0,
                             sector_mask(1), byte_mask_range(32, 32));
    CHECK_EQ(sector_tags.probe(0x2020, idx, sec1, false), SECTOR_MISS);
}

TEST(tag_lru_fifo_flush_invalidate)
{
    cache_config lru_cfg = make_config("N:1:64:2,L:R:m:N:L,A:4:2,8");
    tag_array lru(lru_cfg, 0, 0);
    unsigned idx = 0;
    mem_fetch *a = new_mf(0x0000, 4, false, GLOBAL_ACC_R);
    mem_fetch *b = new_mf(0x0040, 4, false, GLOBAL_ACC_R);
    mem_fetch *c = new_mf(0x0080, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(lru.access(0x0000, 1, idx, a), MISS); lru.fill(idx, 2, a);
    CHECK_EQ(lru.access(0x0040, 3, idx, b), MISS); lru.fill(idx, 4, b);
    CHECK_EQ(lru.access(0x0000, 5, idx, a), HIT);
    CHECK_EQ(lru.access(0x0080, 6, idx, c), MISS); lru.fill(idx, 7, c);
    CHECK_EQ(lru.probe(0x0000, idx, a, false), HIT);
    CHECK_EQ(lru.probe(0x0040, idx, b, false), MISS);

    cache_config fifo_cfg = make_config("N:1:64:2,F:R:m:N:L,A:4:2,8");
    tag_array fifo(fifo_cfg, 0, 0);
    CHECK_EQ(fifo.access(0x0000, 1, idx, a), MISS); fifo.fill(idx, 2, a);
    CHECK_EQ(fifo.access(0x0040, 3, idx, b), MISS); fifo.fill(idx, 4, b);
    CHECK_EQ(fifo.access(0x0000, 5, idx, a), HIT);
    CHECK_EQ(fifo.access(0x0080, 6, idx, c), MISS); fifo.fill(idx, 7, c);
    CHECK_EQ(fifo.probe(0x0000, idx, a, false), MISS);
    CHECK_EQ(fifo.probe(0x0040, idx, b, false), HIT);

    lru.invalidate();
    CHECK_EQ(lru.probe(0x0000, idx, a, false), MISS);
    CHECK_EQ(lru.probe(0x0080, idx, c, false), MISS);

    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    simple_mem_interface mem(64);
    cache_config wb_cfg = make_config("N:2:64:1,L:B:m:F:L,A:4:2,8");
    l1_cache cache("Flush", wb_cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    std::list<cache_event> events;
    mem_fetch *fr = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(cache.access(fr->get_addr(), fr, 1, events), MISS);
    drain_one_level(cache, mem, 2);
    events.clear();
    mem_fetch *fw = new_mf(0x1000, 4, true, GLOBAL_ACC_W);
    CHECK_EQ(cache.access(fw->get_addr(), fw, 3, events), HIT);
    events.clear();
    mem_fetch *clean = new_mf(0x1040, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(cache.access(clean->get_addr(), clean, 4, events), MISS);
    drain_one_level(cache, mem, 5);
    cache.flush();
    events.clear();
    mem_fetch *clean_after_flush = new_mf(0x1040, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(cache.access(clean_after_flush->get_addr(), clean_after_flush, 6,
                          events),
             HIT);
    events.clear();
    mem_fetch *after_flush = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(cache.access(after_flush->get_addr(), after_flush, 7, events), MISS);
    drain_one_level(cache, mem, 8);
    cache.invalidate();
    events.clear();
    mem_fetch *clean_after_invalidate = new_mf(0x1040, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(cache.access(clean_after_invalidate->get_addr(),
                          clean_after_invalidate, 9, events),
             MISS);
}

TEST(tag_on_fill_all_reserved)
{
    cache_config on_fill = make_config("N:1:64:1,L:R:f:N:L,A:4:2,8");
    tag_array tags(on_fill, 0, 0);
    unsigned idx = 0;
    mem_fetch *a = new_mf(0x0000, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(tags.probe(0x0000, idx, a, false), MISS);
    tags.fill(0x0000, 1, a, false);
    CHECK_EQ(tags.probe(0x0000, idx, a, false), HIT);

    cache_config on_miss = make_config("N:1:64:1,L:R:m:N:L,A:4:2,8");
    tag_array one_way(on_miss, 0, 0);
    mem_fetch *reserved = new_mf(0x0000, 4, false, GLOBAL_ACC_R);
    mem_fetch *conflict = new_mf(0x0040, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(one_way.access(0x0000, 2, idx, reserved), MISS);
    CHECK_EQ(one_way.probe(0x0040, idx, conflict, false), RESERVATION_FAIL);
    unsigned accesses = 0;
    unsigned misses = 0;
    unsigned pending_hits = 0;
    unsigned res_fails = 0;
    one_way.access(0x0040, 3, idx, conflict);
    one_way.get_stats(accesses, misses, pending_hits, res_fails);
    CHECK_EQ(res_fails, 1u);
}

TEST(mshr_capacity_order_raw)
{
    mshr_table mshr(2, 2);
    mem_fetch *r0 = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
    mem_fetch *r1 = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
    mem_fetch *r2 = new_mf(0x2000, 4, false, GLOBAL_ACC_R);
    mem_fetch *r3 = new_mf(0x3000, 4, false, GLOBAL_ACC_R);

    CHECK_FALSE(mshr.full(0x1000));
    mshr.add(0x1000, r0);
    CHECK_FALSE(mshr.full(0x1000));
    mshr.add(0x1000, r1);
    CHECK_TRUE(mshr.full(0x1000));
    mshr.add(0x2000, r2);
    CHECK_TRUE(mshr.full(0x3000));

    bool has_atomic = false;
    mshr.mark_ready(0x1000, has_atomic);
    CHECK_TRUE(mshr.access_ready());
    CHECK_TRUE(mshr.next_access() == r0);
    CHECK_TRUE(mshr.next_access() == r1);
    CHECK_FALSE(mshr.access_ready());

    mshr_table raw(4, 4);
    mem_fetch *w = new_mf(0x4000, 4, true, GLOBAL_ACC_W);
    mem_fetch *r = new_mf(0x4000, 4, false, GLOBAL_ACC_R);
    raw.add(0x4000, w);
    CHECK_FALSE(raw.is_read_after_write_pending(0x4000));
    raw.add(0x4000, r);
    CHECK_TRUE(raw.is_read_after_write_pending(0x4000));
}

TEST(read_only_flow_and_fail_stats)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,8");
    read_only_cache cache("RO", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    std::list<cache_event> events;
    mem_fetch *r = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(cache.access(r->get_addr(), r, 1, events), MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    drain_one_level(cache, mem, 2);
    events.clear();
    mem_fetch *hit = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 3, events), HIT);

    simple_mem_interface small_mem(0);
    cache_config small_cfg = make_config("N:2:64:1,L:R:m:N:L,A:2:1,1");
    read_only_cache fail_cache("ROFail", small_cfg, 0, 0, &small_mem,
                               IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    mem_fetch *fail = new_mf(0x2000, 4, false, GLOBAL_ACC_R);
    events.clear();
    CHECK_EQ(fail_cache.access(fail->get_addr(), fail, 4, events), MISS);
    mem_fetch *miss_queue_full = new_mf(0x2040, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(fail_cache.access(miss_queue_full->get_addr(), miss_queue_full, 5,
                               events),
             RESERVATION_FAIL);
    CHECK_EQ(fail_cache.get_fail_stats(GLOBAL_ACC_R, MISS_QUEUE_FULL), 1ull);
    fail_cache.cycle();
    CHECK_EQ(small_mem.queue.size(), 0u);
}

TEST(fail_reason_counters)
{
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    std::list<cache_event> events;

    {
        simple_mem_interface mem(64);
        cache_config cfg = make_config("N:1:64:1,L:B:m:F:L,A:4:2,8");
        l1_cache cache("LineAllocFail", cfg, 0, 0, &mem, &allocator,
                       IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
        mem_fetch *first = new_mf(0x0000, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(first->get_addr(), first, 1, events), MISS);
        events.clear();
        mem_fetch *conflict = new_mf(0x0040, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(conflict->get_addr(), conflict, 2, events),
                 RESERVATION_FAIL);
        CHECK_EQ(cache.get_fail_stats(GLOBAL_ACC_R, LINE_ALLOC_FAIL), 1ull);
    }

    {
        simple_mem_interface mem(64);
        cache_config cfg = make_config("N:4:64:4,L:B:m:F:L,A:1:4,8");
        l1_cache cache("MshrEntryFail", cfg, 0, 0, &mem, &allocator,
                       IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
        events.clear();
        mem_fetch *first = new_mf(0x0000, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(first->get_addr(), first, 1, events), MISS);
        events.clear();
        mem_fetch *second = new_mf(0x0040, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(second->get_addr(), second, 2, events),
                 RESERVATION_FAIL);
        CHECK_EQ(cache.get_fail_stats(GLOBAL_ACC_R, MSHR_ENRTY_FAIL), 1ull);
    }

    {
        simple_mem_interface mem(64);
        cache_config cfg = make_config("N:4:64:4,L:B:m:F:L,A:1:1,8");
        l1_cache cache("MshrMergeFail", cfg, 0, 0, &mem, &allocator,
                       IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
        events.clear();
        mem_fetch *first = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(first->get_addr(), first, 1, events), MISS);
        events.clear();
        mem_fetch *merged = new_mf(0x1004, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(merged->get_addr(), merged, 2, events),
                 RESERVATION_FAIL);
        CHECK_EQ(cache.get_fail_stats(GLOBAL_ACC_R, MSHR_MERGE_ENRTY_FAIL),
                 1ull);
    }

    cache_stats stats;
    stats.clear();
    stats.inc_fail_stats(GLOBAL_ACC_R, LINE_ALLOC_FAIL, 0);
    stats.inc_fail_stats(GLOBAL_ACC_R, MISS_QUEUE_FULL, 0);
    stats.inc_fail_stats(GLOBAL_ACC_R, MSHR_ENRTY_FAIL, 0);
    stats.inc_fail_stats(GLOBAL_ACC_R, MSHR_MERGE_ENRTY_FAIL, 0);
    stats.inc_fail_stats(GLOBAL_ACC_W, MSHR_RW_PENDING, 0);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, LINE_ALLOC_FAIL), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, MISS_QUEUE_FULL), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, MSHR_ENRTY_FAIL), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, MSHR_MERGE_ENRTY_FAIL), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_W, MSHR_RW_PENDING), 1ull);
}

TEST(data_cache_read_and_write_policies)
{
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    {
        simple_mem_interface mem(64);
        cache_config cfg = make_config("N:4:64:2,L:B:m:F:L,A:8:4,16");
        l1_cache cache("WB", cfg, 0, 0, &mem, &allocator, IN_L1D_MISS_QUEUE,
                       &gpu, L1_GPU_CACHE);
        std::list<cache_event> events;
        mem_fetch *r = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(r->get_addr(), r, 1, events), MISS);
        drain_one_level(cache, mem, 2);
        events.clear();
        mem_fetch *w = new_mf(0x1000, 4, true, GLOBAL_ACC_W);
        CHECK_EQ(cache.access(w->get_addr(), w, 3, events), HIT);
        CHECK_FALSE(has_event(events, WRITE_REQUEST_SENT));
    }

    {
        simple_mem_interface mem(64);
        cache_config cfg = make_config("N:4:64:2,L:T:m:N:L,A:8:4,16");
        l1_cache cache("WT", cfg, 0, 0, &mem, &allocator, IN_L1D_MISS_QUEUE,
                       &gpu, L1_GPU_CACHE);
        std::list<cache_event> events;
        mem_fetch *r = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(r->get_addr(), r, 1, events), MISS);
        drain_one_level(cache, mem, 2);
        events.clear();
        mem_fetch *w = new_mf(0x1000, 4, true, GLOBAL_ACC_W);
        CHECK_EQ(cache.access(w->get_addr(), w, 3, events), HIT);
        CHECK_TRUE(has_event(events, WRITE_REQUEST_SENT));
    }

    {
        simple_mem_interface mem(64);
        cache_config cfg = make_config("N:4:64:2,L:E:m:N:L,A:8:4,16");
        l1_cache cache("WE", cfg, 0, 0, &mem, &allocator, IN_L1D_MISS_QUEUE,
                       &gpu, L1_GPU_CACHE);
        std::list<cache_event> events;
        mem_fetch *r = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(r->get_addr(), r, 1, events), MISS);
        drain_one_level(cache, mem, 2);
        events.clear();
        mem_fetch *w = new_mf(0x1000, 4, true, GLOBAL_ACC_W);
        CHECK_EQ(cache.access(w->get_addr(), w, 3, events), HIT);
        CHECK_TRUE(has_event(events, WRITE_REQUEST_SENT));
        events.clear();
        mem_fetch *again = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(again->get_addr(), again, 4, events), MISS);
    }

    {
        simple_mem_interface mem(64);
        cache_config cfg = make_config("N:4:64:2,L:L:m:N:L,A:8:4,16");
        l1_cache cache("LocalWbGlobalWt", cfg, 0, 0, &mem, &allocator,
                       IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
        std::list<cache_event> events;
        mem_fetch *global_r = new_mf(0x1000, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(global_r->get_addr(), global_r, 1, events),
                 MISS);
        drain_one_level(cache, mem, 2);
        events.clear();
        mem_fetch *global_w = new_mf(0x1000, 4, true, GLOBAL_ACC_W);
        CHECK_EQ(cache.access(global_w->get_addr(), global_w, 3, events),
                 HIT);
        CHECK_TRUE(has_event(events, WRITE_REQUEST_SENT));

        simple_mem_interface local_mem(64);
        l1_cache local_cache("LocalWbGlobalWtLocal", cfg, 0, 0, &local_mem,
                             &allocator, IN_L1D_MISS_QUEUE, &gpu,
                             L1_GPU_CACHE);
        events.clear();
        mem_fetch *local_r = new_mf(0x1080, 4, false, LOCAL_ACC_R);
        CHECK_EQ(local_cache.access(local_r->get_addr(), local_r, 5, events),
                 MISS);
        drain_one_level(local_cache, local_mem, 6);
        events.clear();
        mem_fetch *local_w = new_mf(0x1080, 4, true, LOCAL_ACC_W);
        CHECK_EQ(local_cache.access(local_w->get_addr(), local_w, 7, events),
                 HIT);
        CHECK_FALSE(has_event(events, WRITE_REQUEST_SENT));
    }
}

TEST(write_miss_events_and_writeback)
{
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    simple_mem_interface nwa_mem(64);
    cache_config nwa_cfg = make_config("N:4:64:2,L:T:m:N:L,A:8:4,16");
    l1_cache nwa("NWA", nwa_cfg, 0, 0, &nwa_mem, &allocator,
                 IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    std::list<cache_event> events;
    mem_fetch *w0 = new_mf(0x1000, 4, true, GLOBAL_ACC_W);
    CHECK_EQ(nwa.access(w0->get_addr(), w0, 1, events), MISS);
    CHECK_TRUE(has_event(events, WRITE_REQUEST_SENT));
    CHECK_FALSE(has_event(events, READ_REQUEST_SENT));

    simple_mem_interface wa_mem(64);
    cache_config wa_cfg = make_config("N:4:64:2,L:B:m:W:L,A:8:4,16");
    l1_cache wa("WA", wa_cfg, 0, 0, &wa_mem, &allocator,
                IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    events.clear();
    mem_fetch *w1 = new_mf(0x2000, 4, true, GLOBAL_ACC_W);
    CHECK_EQ(wa.access(w1->get_addr(), w1, 1, events), MISS);
    CHECK_TRUE(has_event(events, WRITE_REQUEST_SENT));
    CHECK_TRUE(has_event(events, WRITE_ALLOCATE_SENT));
    wa.cycle();
    wa.cycle();
    CHECK_EQ(wa_mem.queue.size(), 2u);

    simple_mem_interface fow_mem(64);
    cache_config fow_cfg = make_config("N:4:64:2,L:B:m:F:L,A:8:4,16");
    l1_cache fow("FetchOnWrite", fow_cfg, 0, 0, &fow_mem, &allocator,
                 IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    events.clear();
    mem_fetch *full_write = new_mf(0x2400, 64, true, GLOBAL_ACC_W, 1,
                                   sector_mask(0), byte_mask_range(0, 64));
    CHECK_EQ(fow.access(full_write->get_addr(), full_write, 1, events), MISS);
    CHECK_FALSE(has_event(events, READ_REQUEST_SENT));
    CHECK_FALSE(has_event(events, WRITE_ALLOCATE_SENT));
    events.clear();
    mem_fetch *partial_write = new_mf(0x2480, 4, true, GLOBAL_ACC_W, 2,
                                      sector_mask(0), byte_mask_range(0, 4));
    CHECK_EQ(fow.access(partial_write->get_addr(), partial_write, 2, events),
             MISS);
    CHECK_TRUE(has_event(events, WRITE_ALLOCATE_SENT));

    simple_mem_interface wb_mem(64);
    cache_config wb_cfg = make_config("N:1:64:1,L:B:m:F:L,A:8:4,16");
    l1_cache wb("DirtyEvict", wb_cfg, 0, 0, &wb_mem, &allocator,
                IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    events.clear();
    mem_fetch *r = new_mf(0x0000, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(wb.access(r->get_addr(), r, 1, events), MISS);
    drain_one_level(wb, wb_mem, 2);
    events.clear();
    mem_fetch *w = new_mf(0x0000, 64, true, GLOBAL_ACC_W, 3,
                          sector_mask(0), byte_mask_range(0, 64));
    CHECK_EQ(wb.access(w->get_addr(), w, 3, events), HIT);
    events.clear();
    mem_fetch *r2 = new_mf(0x0040, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(wb.access(r2->get_addr(), r2, 4, events), MISS);
    CHECK_TRUE(has_event(events, WRITE_BACK_REQUEST_SENT));

    simple_mem_interface lazy_mem(64);
    cache_config lazy_cfg = make_config("S:2:128:1,L:B:m:L:L,A:8:4,16");
    l1_cache lazy("LazyPartial", lazy_cfg, 0, 0, &lazy_mem, &allocator,
                  IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    events.clear();
    mem_fetch *partial = new_mf(0x3000, 4, true, GLOBAL_ACC_W, 1,
                                sector_mask(0), byte_mask_range(0, 4));
    CHECK_EQ(lazy.access(partial->get_addr(), partial, 1, events), MISS);
    CHECK_FALSE(has_event(events, READ_REQUEST_SENT));
    events.clear();
    mem_fetch *read_partial = new_mf(0x3000, 4, false, GLOBAL_ACC_R, 2,
                                     sector_mask(0), byte_mask_range(0, 4));
    CHECK_EQ(lazy.access(read_partial->get_addr(), read_partial, 2, events),
             MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    drain_one_level(lazy, lazy_mem, 3);
    events.clear();
    mem_fetch *read_after_fill = new_mf(0x3000, 4, false, GLOBAL_ACC_R, 4,
                                        sector_mask(0), byte_mask_range(0, 4));
    CHECK_EQ(lazy.access(read_after_fill->get_addr(), read_after_fill, 4,
                         events),
             HIT);

    simple_mem_interface full_mem(64);
    l1_cache full_lazy("LazyFull", lazy_cfg, 0, 0, &full_mem, &allocator,
                       IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    mem_fetch *full = new_mf(0x4000, SECTOR_SIZE, true, GLOBAL_ACC_W, 1,
                             sector_mask(0), byte_mask_range(0, SECTOR_SIZE));
    events.clear();
    CHECK_EQ(full_lazy.access(full->get_addr(), full, 1, events), MISS);
    events.clear();
    mem_fetch *read_full = new_mf(0x4000, 4, false, GLOBAL_ACC_R, 2,
                                  sector_mask(0), byte_mask_range(0, 4));
    CHECK_EQ(full_lazy.access(read_full->get_addr(), read_full, 2, events),
             HIT);
    CHECK_FALSE(has_event(events, READ_REQUEST_SENT));
}

TEST(sector_dirty_masks_and_writeback)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("S:1:128:1,L:B:m:F:L,A:8:4,16");
    l1_cache cache("SectorDirty", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    std::list<cache_event> events;

    mem_fetch *write_s0 = new_mf(0x0000, SECTOR_SIZE, true, GLOBAL_ACC_W, 1,
                                 sector_mask(0), byte_mask_range(0, SECTOR_SIZE));
    CHECK_EQ(cache.access(write_s0->get_addr(), write_s0, 1, events), MISS);

    events.clear();
    mem_fetch *write_s2 = new_mf(0x0040, SECTOR_SIZE, true, GLOBAL_ACC_W, 2,
                                 sector_mask(2), byte_mask_range(64, SECTOR_SIZE));
    CHECK_EQ(cache.access(write_s2->get_addr(), write_s2, 2, events), MISS);

    events.clear();
    mem_fetch *evict = new_mf(0x0080, SECTOR_SIZE, true, GLOBAL_ACC_W, 3,
                              sector_mask(0), byte_mask_range(0, SECTOR_SIZE));
    CHECK_EQ(cache.access(evict->get_addr(), evict, 3, events), MISS);
    CHECK_TRUE(has_event(events, WRITE_BACK_REQUEST_SENT));
    cache_event wb = find_event(events, WRITE_BACK_REQUEST_SENT);
    CHECK_EQ(wb.m_evicted_block.m_block_addr, 0ull);
    CHECK_EQ(wb.m_evicted_block.m_modified_size, (unsigned)(2 * SECTOR_SIZE));
    CHECK_TRUE(wb.m_evicted_block.m_sector_mask.test(0));
    CHECK_FALSE(wb.m_evicted_block.m_sector_mask.test(1));
    CHECK_TRUE(wb.m_evicted_block.m_sector_mask.test(2));
    CHECK_FALSE(wb.m_evicted_block.m_sector_mask.test(3));
}

TEST(texture_pipeline_and_backpressure)
{
    simple_mem_interface mem(64);
    cache_config cfg = make_config("N:4:128:4,L:R:m:N:L,F:4:2,4:2");
    tex_cache cache("Tex", cfg, 0, 0, &mem, IN_L1T_MISS_QUEUE,
                    IN_SHADER_L1T_ROB);
    std::list<cache_event> events;
    mem_fetch *r = new_mf(0x1000, 4, false, TEXTURE_ACC_R);
    CHECK_EQ(cache.access(r->get_addr(), r, 1, events), MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    cache.cycle();
    CHECK_EQ(mem.queue.size(), 1u);
    mem_fetch *resp = mem.queue.front();
    mem.queue.pop_front();
    cache.fill(resp, 2);
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == r);

    events.clear();
    mem_fetch *hit = new_mf(0x1000, 4, false, TEXTURE_ACC_R);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 3, events), HIT_RESERVED);
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == hit);

    simple_mem_interface bp_mem(0);
    cache_config tiny = make_config("N:4:128:4,L:R:m:N:L,F:1:1,1:1");
    tex_cache bp("TexBP", tiny, 0, 0, &bp_mem, IN_L1T_MISS_QUEUE,
                 IN_SHADER_L1T_ROB);
    mem_fetch *first = new_mf(0x2000, 4, false, TEXTURE_ACC_R);
    mem_fetch *second = new_mf(0x2080, 4, false, TEXTURE_ACC_R);
    events.clear();
    CHECK_EQ(bp.access(first->get_addr(), first, 1, events), MISS);
    CHECK_EQ(bp.access(second->get_addr(), second, 2, events), RESERVATION_FAIL);

    simple_mem_interface rf_mem(64);
    cache_config rf_cfg = make_config("N:4:128:4,L:R:m:N:L,F:4:4,4:1");
    tex_cache rf("TexResultFIFO", rf_cfg, 0, 0, &rf_mem, IN_L1T_MISS_QUEUE,
                 IN_SHADER_L1T_ROB);
    events.clear();
    mem_fetch *miss = new_mf(0x3000, 4, false, TEXTURE_ACC_R);
    CHECK_EQ(rf.access(miss->get_addr(), miss, 1, events), MISS);
    rf.cycle();
    CHECK_EQ(rf_mem.queue.size(), 1u);
    mem_fetch *miss_resp = rf_mem.queue.front();
    rf_mem.queue.pop_front();
    rf.fill(miss_resp, 2);
    rf.cycle();
    CHECK_TRUE(rf.access_ready());

    events.clear();
    mem_fetch *rf_hit = new_mf(0x3000, 4, false, TEXTURE_ACC_R);
    CHECK_EQ(rf.access(rf_hit->get_addr(), rf_hit, 3, events), HIT_RESERVED);
    rf.cycle();
    CHECK_TRUE(rf.access_ready());
    CHECK_TRUE(rf.next_access() == miss);
    CHECK_FALSE(rf.access_ready());
    rf.cycle();
    CHECK_TRUE(rf.access_ready());
    CHECK_TRUE(rf.next_access() == rf_hit);
}

TEST(stats_datastore_property_trace)
{
    cache_stats stats;
    stats.clear();
    stats.inc_stats(GLOBAL_ACC_R, HIT, 0);
    stats.inc_stats(GLOBAL_ACC_R, MISS, 0);
    stats.inc_stats(GLOBAL_ACC_R, RESERVATION_FAIL, 0);
    stats.inc_fail_stats(GLOBAL_ACC_R, MISS_QUEUE_FULL, 0);
    cache_sub_stats css;
    stats.get_sub_stats(css);
    CHECK_EQ(css.accesses, 2ull);
    CHECK_EQ(css.misses, 1ull);
    CHECK_EQ(css.res_fails, 1ull);
    cache_sub_stats_pw pw;
    stats.get_sub_stats_pw(pw);
    CHECK_EQ(pw.accesses, 0u);
    stats.inc_stats_pw(GLOBAL_ACC_W, HIT, 0);
    stats.inc_stats_pw(GLOBAL_ACC_R, MISS, 0);
    stats.get_sub_stats_pw(pw);
    CHECK_EQ(pw.accesses, 2u);
    CHECK_EQ(pw.write_hits, 1u);
    CHECK_EQ(pw.read_misses, 1u);
    stats.clear_pw();
    stats.get_sub_stats_pw(pw);
    CHECK_EQ(pw.accesses, 0u);

    DataStore store;
    CHECK_FALSE(store.contains(0x1000));
    std::vector<uint8_t> zeros = store.read(0x1000, 4);
    CHECK_EQ(zeros.size(), 4u);
    CHECK_EQ(zeros[0], 0u);
    uint8_t data[4] = {1, 2, 3, 4};
    store.write(0x1000, data, 4);
    CHECK_TRUE(store.contains(0x1000));
    std::vector<uint8_t> read = store.read(0x1000, 4);
    CHECK_EQ(read[2], 3u);
    uint8_t partial[2] = {9, 8};
    store.write(0x1000, partial, 2);
    std::vector<uint8_t> preserved = store.read(0x1000, 4);
    CHECK_EQ(preserved[0], 9u);
    CHECK_EQ(preserved[1], 8u);
    CHECK_EQ(preserved[2], 3u);
    CHECK_EQ(preserved[3], 4u);

    simple_mem_interface mem(512);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:16:64:4,L:R:m:N:L,A:16:4,32");
    read_only_cache cache("Prop", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    std::mt19937 rng(0xCACE2026);
    unsigned accepted = 0;
    for (unsigned i = 0; i < 256; ++i) {
        new_addr_type addr = (rng() % 4096) & ~0x3ull;
        mem_fetch *mf = new_mf(addr, 4, false, GLOBAL_ACC_R, i);
        std::list<cache_event> events;
        cache_request_status status = cache.access(mf->get_addr(), mf, i, events);
        if (status != RESERVATION_FAIL)
            accepted++;
        drain_one_level(cache, mem, i);
    }
    cache_sub_stats prop_stats;
    cache.get_sub_stats(prop_stats);
    CHECK_EQ(prop_stats.accesses, (unsigned long long)accepted);
    CHECK_EQ(prop_stats.res_fails, 0ull);
}

TEST(port_timing_visibility)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:B:m:F:L,A:4:2,16:1,8");
    l1_cache cache("Ports", cfg, 0, 0, &mem, &allocator, IN_L1D_MISS_QUEUE,
                   &gpu, L1_GPU_CACHE);
    CHECK_TRUE(cache.data_port_free());
    CHECK_TRUE(cache.fill_port_free());

    std::list<cache_event> warm_events;
    mem_fetch *warm = new_mf(0x1000, 16, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(cache.access(warm->get_addr(), warm, 1, warm_events), MISS);
    drain_one_level(cache, mem, 2);
    std::list<cache_event> events;
    mem_fetch *hit = new_mf(0x1000, 16, false, GLOBAL_ACC_R, 3);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 3, events), HIT);
    CHECK_FALSE(cache.data_port_free());
    cache.cycle();
    CHECK_FALSE(cache.data_port_free());
    cache.cycle();
    CHECK_TRUE(cache.data_port_free());

    mem_fetch *miss = new_mf(0x2000, 16, false, GLOBAL_ACC_R, 4);
    events.clear();
    CHECK_EQ(cache.access(miss->get_addr(), miss, 4, events), MISS);
    cache.cycle();
    CHECK_EQ(mem.queue.size(), 1u);
    mem_fetch *resp = mem.queue.front();
    mem.queue.pop_front();
    cache.fill(resp, 5);
    CHECK_FALSE(cache.fill_port_free());
    for (unsigned i = 0; i < 128 && !cache.fill_port_free(); ++i)
        cache.cycle();
    CHECK_TRUE(cache.fill_port_free());
}

int main()
{
    printf("\n========== GPGPU-Sim Cache Deep Whitebox Test Suite ==========\n\n");

    RUN_TEST(config_sector_and_streaming);
    RUN_TEST(address_mapping_boundaries);
    RUN_TEST(tag_fill_hit_reserved_and_sector_miss);
    RUN_TEST(tag_lru_fifo_flush_invalidate);
    RUN_TEST(tag_on_fill_all_reserved);
    RUN_TEST(mshr_capacity_order_raw);
    RUN_TEST(read_only_flow_and_fail_stats);
    RUN_TEST(fail_reason_counters);
    RUN_TEST(data_cache_read_and_write_policies);
    RUN_TEST(write_miss_events_and_writeback);
    RUN_TEST(sector_dirty_masks_and_writeback);
    RUN_TEST(texture_pipeline_and_backpressure);
    RUN_TEST(stats_datastore_property_trace);
    RUN_TEST(port_timing_visibility);

    printf("\n========== Results: %d/%d tests passed ==========\n",
           tests_passed, tests_run);
    if (tests_failed)
        printf("========== Failures: %d ==========\n", tests_failed);

    return tests_failed == 0 && tests_passed == tests_run ? 0 : 1;
}
