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

struct set_index_golden {
    new_addr_type addr;
    unsigned linear;
    unsigned xoring;
    unsigned ipoly;
    unsigned custom;
    unsigned fermi32;
    unsigned fermi64;
};

struct config_axis_case {
    const char *label;
    const char *text;
    unsigned nset;
    unsigned line_sz;
    unsigned assoc;
    unsigned atom_sz;
    unsigned data_port_width;
    enum mshr_config_t mshr_type;
    enum write_policy_t write_policy;
    enum write_allocate_policy_t write_alloc_policy;
    bool streaming;
};

struct pairwise_smoke_case {
    const char *label;
    const char *text;
    bool texture;
    bool sector_texture;
    bool sector_assoc;
};

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

static mem_fetch *new_child_mf(new_addr_type addr, unsigned size, bool wr,
                               mem_access_type type, mem_fetch *original,
                               unsigned cycle = 0,
                               mem_access_sector_mask_t sm = sector_mask(0),
                               mem_access_byte_mask_t bm = byte_mask_range(0, MAX_MEMORY_ACCESS_SIZE))
{
    mem_access_t access(type, addr, size, wr, active_mask_t(), bm, sm);
    warp_inst_t *inst = new warp_inst_t();
    inst->m_is_load = !wr;
    inst->m_is_store = wr;
    inst->m_is_write = wr;
    return new mem_fetch(access, inst, 0, 0, 0, 0, 0, NULL, cycle, original);
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
    for (unsigned step = 0; step < 256; ++step) {
        cache.cycle();
        while (!mem.queue.empty()) {
            mem_fetch *resp = mem.queue.front();
            mem.queue.pop_front();
            if (cache.waiting_for_fill(resp))
                cache.fill(resp, cycle + step);
        }
        while (cache.access_ready())
            cache.next_access();
        if (mem.queue.empty() && cache.queues_empty())
            return;
    }
    CHECK_TRUE(false);
}

static void drain_texture(tex_cache &cache, simple_mem_interface &mem,
                          unsigned cycle)
{
    for (unsigned step = 0; step < 256; ++step) {
        cache.cycle();
        while (!mem.queue.empty()) {
            mem_fetch *resp = mem.queue.front();
            mem.queue.pop_front();
            cache.fill(resp, cycle + step);
        }
        while (cache.access_ready())
            cache.next_access();
        if (mem.queue.empty() && cache.queues_empty())
            return;
    }
    CHECK_TRUE(false);
}

static void final_check(baseline_cache &cache, simple_mem_interface &mem,
                        unsigned cycle)
{
    drain_one_level(cache, mem, cycle);
    CHECK_TRUE(cache.no_pending_accesses());
    CHECK_TRUE(mem.queue.empty());
    cache.invalidate();
    CHECK_TRUE(cache.final_state_clean());
}

static void final_check(tex_cache &cache, simple_mem_interface &mem,
                        unsigned cycle)
{
    drain_texture(cache, mem, cycle);
    CHECK_TRUE(cache.no_pending_accesses());
    CHECK_TRUE(mem.queue.empty());
    cache.invalidate();
    CHECK_TRUE(cache.final_state_clean());
}

class baseline_final_check_guard {
public:
    baseline_final_check_guard(baseline_cache &cache, simple_mem_interface &mem,
                               unsigned cycle = 0)
        : m_cache(cache), m_mem(mem), m_cycle(cycle), m_active(true) {}
    ~baseline_final_check_guard()
    {
        if (m_active)
            final_check(m_cache, m_mem, m_cycle);
    }
    void dismiss() { m_active = false; }

private:
    baseline_cache &m_cache;
    simple_mem_interface &m_mem;
    unsigned m_cycle;
    bool m_active;
};

class texture_final_check_guard {
public:
    texture_final_check_guard(tex_cache &cache, simple_mem_interface &mem,
                              unsigned cycle = 0)
        : m_cache(cache), m_mem(mem), m_cycle(cycle), m_active(true) {}
    ~texture_final_check_guard()
    {
        if (m_active)
            final_check(m_cache, m_mem, m_cycle);
    }
    void dismiss() { m_active = false; }

private:
    tex_cache &m_cache;
    simple_mem_interface &m_mem;
    unsigned m_cycle;
    bool m_active;
};

class tag_array_final_check_guard {
public:
    explicit tag_array_final_check_guard(tag_array &tags)
        : m_tags(tags), m_active(true) {}
    ~tag_array_final_check_guard()
    {
        if (!m_active)
            return;
        CHECK_TRUE(m_tags.no_pending_accesses());
        m_tags.invalidate();
        CHECK_TRUE(m_tags.final_state_clean());
    }
    void dismiss() { m_active = false; }

private:
    tag_array &m_tags;
    bool m_active;
};

class mshr_final_check_guard {
public:
    explicit mshr_final_check_guard(mshr_table &mshr)
        : m_mshr(mshr), m_active(true) {}
    ~mshr_final_check_guard()
    {
        if (m_active)
            CHECK_TRUE(m_mshr.empty());
    }
    void dismiss() { m_active = false; }

private:
    mshr_table &m_mshr;
    bool m_active;
};

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
    CHECK_TRUE(normal.defer_hit_response());
    CHECK_EQ(normal.get_hit_response_queue_size(), 16u);

    cache_config sector = make_config("S:4:128:2,L:B:m:F:X,A:4:2,8");
    CHECK_EQ(sector.get_atom_sz(), (unsigned)SECTOR_SIZE);

    cache_config streaming = make_config("N:8:64:2,L:R:s:N:L,A:8:2,16");
    CHECK_TRUE(streaming.is_streaming());
}

TEST(status_string_tables)
{
    CHECK_EQ(strcmp(cache_request_status_str(HIT), "HIT"), 0);
    CHECK_EQ(strcmp(cache_request_status_str(HIT_RESERVED), "HIT_RESERVED"), 0);
    CHECK_EQ(strcmp(cache_request_status_str(MISS), "MISS"), 0);
    CHECK_EQ(strcmp(cache_request_status_str(RESERVATION_FAIL),
                    "RESERVATION_FAIL"),
             0);
    CHECK_EQ(strcmp(cache_request_status_str(SECTOR_MISS), "SECTOR_MISS"), 0);
    CHECK_EQ(strcmp(cache_request_status_str(MSHR_HIT), "MSHR_HIT"), 0);

    CHECK_EQ(strcmp(cache_fail_status_str(LINE_ALLOC_FAIL),
                    "LINE_ALLOC_FAIL"),
             0);
    CHECK_EQ(strcmp(cache_fail_status_str(MISS_QUEUE_FULL), "MISS_QUEUE_FULL"),
             0);
    CHECK_EQ(strcmp(cache_fail_status_str(MSHR_ENRTY_FAIL),
                    "MSHR_ENRTY_FAIL"),
             0);
    CHECK_EQ(strcmp(cache_fail_status_str(MSHR_MERGE_ENRTY_FAIL),
                    "MSHR_MERGE_ENRTY_FAIL"),
             0);
    CHECK_EQ(strcmp(cache_fail_status_str(MSHR_RW_PENDING),
                    "MSHR_RW_PENDING"),
             0);
    CHECK_EQ(strcmp(cache_fail_status_str(LINE_PINNED_FAIL),
                    "LINE_PINNED_FAIL"),
             0);
    CHECK_EQ(strcmp(cache_fail_status_str(HIT_RESPONSE_QUEUE_FULL),
                    "HIT_RESPONSE_QUEUE_FULL"),
             0);
}

TEST(parameter_single_axis_matrix)
{
    const config_axis_case cases[] = {
        {"nset_min", "N:1:64:1,L:R:m:N:L,A:1:1,64",
         1u, 64u, 1u, 64u, 64u, ASSOC, READ_ONLY, NO_WRITE_ALLOCATE, false},
        {"nset_mid_assoc2", "N:16:64:2,L:R:m:N:L,A:4:2,16",
         16u, 64u, 2u, 64u, 64u, ASSOC, READ_ONLY, NO_WRITE_ALLOCATE, false},
        {"nset_large_assoc16", "N:256:128:16,L:B:m:F:L,A:64:8,128",
         256u, 128u, 16u, 128u, 128u, ASSOC, WRITE_BACK, FETCH_ON_WRITE, false},
        {"sector_atom", "S:4:128:4,L:B:m:F:L,A:4:2,8",
         4u, 128u, 4u, (unsigned)SECTOR_SIZE, 128u, ASSOC, WRITE_BACK,
         FETCH_ON_WRITE, false},
        {"fifo_replacement", "N:4:64:2,F:R:m:N:L,A:4:2,8",
         4u, 64u, 2u, 64u, 64u, ASSOC, READ_ONLY, NO_WRITE_ALLOCATE, false},
        {"write_through", "N:4:64:2,L:T:m:N:L,A:8:4,16",
         4u, 64u, 2u, 64u, 64u, ASSOC, WRITE_THROUGH, NO_WRITE_ALLOCATE, false},
        {"write_evict", "N:4:64:2,L:E:m:N:L,A:8:4,16",
         4u, 64u, 2u, 64u, 64u, ASSOC, WRITE_EVICT, NO_WRITE_ALLOCATE, false},
        {"local_wb_global_wt", "N:4:64:2,L:L:m:N:L,A:8:4,16",
         4u, 64u, 2u, 64u, 64u, ASSOC, LOCAL_WB_GLOBAL_WT,
         NO_WRITE_ALLOCATE, false},
        {"write_allocate", "N:4:64:2,L:B:m:W:L,A:8:4,16",
         4u, 64u, 2u, 64u, 64u, ASSOC, WRITE_BACK, WRITE_ALLOCATE, false},
        {"lazy_fetch", "S:2:128:1,L:B:m:L:L,A:8:4,16",
         2u, 128u, 1u, (unsigned)SECTOR_SIZE, 128u, ASSOC, WRITE_BACK,
         LAZY_FETCH_ON_READ, false},
        {"streaming", "N:8:64:2,L:R:s:N:L,A:8:2,16",
         8u, 64u, 2u, 64u, 64u, ASSOC, READ_ONLY, NO_WRITE_ALLOCATE, true},
        {"sector_assoc", "S:4:128:4,L:B:m:N:L,S:4:2,8",
         4u, 128u, 4u, (unsigned)SECTOR_SIZE, 128u, SECTOR_ASSOC,
         WRITE_BACK, NO_WRITE_ALLOCATE, false},
        {"tex_fifo", "N:4:128:4,L:R:m:N:L,F:4:2,4:2",
         4u, 128u, 4u, 128u, 128u, TEX_FIFO, READ_ONLY, NO_WRITE_ALLOCATE,
         false},
        {"sector_tex_fifo", "S:4:128:4,L:R:m:N:L,T:4:2,4:2",
         4u, 128u, 4u, (unsigned)SECTOR_SIZE, 128u, SECTOR_TEX_FIFO,
         READ_ONLY, NO_WRITE_ALLOCATE, false},
        {"explicit_data_port", "N:4:64:2,L:B:m:F:L,A:4:2,16:1,8",
         4u, 64u, 2u, 64u, 8u, ASSOC, WRITE_BACK, FETCH_ON_WRITE, false},
        {"xor_index", "N:16:64:2,L:R:m:N:X,A:8:2,16",
         16u, 64u, 2u, 64u, 64u, ASSOC, READ_ONLY, NO_WRITE_ALLOCATE, false},
        {"ipoly_index", "N:16:64:2,L:R:m:N:P,A:8:2,16",
         16u, 64u, 2u, 64u, 64u, ASSOC, READ_ONLY, NO_WRITE_ALLOCATE, false},
        {"fermi_index", "N:32:64:2,L:R:m:N:H,A:8:2,16",
         32u, 64u, 2u, 64u, 64u, ASSOC, READ_ONLY, NO_WRITE_ALLOCATE, false},
    };

    for (const config_axis_case &entry : cases) {
        cache_config cfg = make_config(entry.text);
        CHECK_EQ(cfg.get_nset(), entry.nset);
        CHECK_EQ(cfg.get_line_sz(), entry.line_sz);
        CHECK_EQ(cfg.get_num_lines(), entry.nset * entry.assoc);
        CHECK_EQ(cfg.get_atom_sz(), entry.atom_sz);
        CHECK_EQ(cfg.get_data_port_width(), entry.data_port_width);
        CHECK_EQ((unsigned)cfg.get_mshr_type(), (unsigned)entry.mshr_type);
        CHECK_EQ((unsigned)cfg.get_write_policy(), (unsigned)entry.write_policy);
        CHECK_EQ((unsigned)cfg.get_write_allocate_policy(),
                 (unsigned)entry.write_alloc_policy);
        CHECK_EQ(cfg.is_streaming(), entry.streaming);
        CHECK_TRUE(cfg.set_index(0x12340) < cfg.get_nset());
        CHECK_EQ(cfg.get_total_size_inKB(),
                 entry.nset * entry.assoc * entry.line_sz / 1024);
    }
}

static void run_data_smoke(const char *text, bool sector_assoc)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config(text);
    if (cfg.get_write_policy() == READ_ONLY) {
        read_only_cache cache("ParamReadOnly", cfg, 0, 0, &mem,
                              IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
        baseline_final_check_guard final_cache(cache, mem);
        std::list<cache_event> events;
        mem_fetch *read = new_mf(0x1000, 4, false, GLOBAL_ACC_R, 1);
        CHECK_EQ(cache.access(read->get_addr(), read, 1, events), MISS);
        CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
        drain_one_level(cache, mem, 2);
        events.clear();
        mem_fetch *hit = new_mf(0x1000, 4, false, GLOBAL_ACC_R, 3);
        CHECK_EQ(cache.access(hit->get_addr(), hit, 3, events), HIT);
        return;
    }

    l1_cache cache("ParamData", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;
    mem_fetch *read = new_mf(0x1000, 4, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(cache.access(read->get_addr(), read, 1, events), MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    cache.cycle();
    CHECK_EQ(mem.queue.size(), 1u);
    mem_fetch *req = mem.queue.front();
    mem.queue.pop_front();
    if (sector_assoc) {
        for (unsigned sector = 0; sector < 3; ++sector) {
            mem_fetch *partial = new_child_mf(0x1000 + sector * SECTOR_SIZE,
                                              SECTOR_SIZE, false, GLOBAL_ACC_R,
                                              req, 2 + sector,
                                              sector_mask(sector),
                                              byte_mask_range(sector * SECTOR_SIZE,
                                                              SECTOR_SIZE));
            cache.fill(partial, 2 + sector);
            CHECK_FALSE(cache.access_ready());
        }
        mem_fetch *final_resp = new_child_mf(0x1000 + 3 * SECTOR_SIZE,
                                             SECTOR_SIZE, false, GLOBAL_ACC_R,
                                             req, 5, sector_mask(3),
                                             byte_mask_range(3 * SECTOR_SIZE,
                                                             SECTOR_SIZE));
        cache.fill(final_resp, 5);
    } else {
        cache.fill(req, 2);
    }
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == read);

    events.clear();
    mem_fetch *hit = new_mf(0x1000, 4, false, GLOBAL_ACC_R, 6);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 6, events), HIT);
}

static void run_texture_smoke(const char *text, bool sector_texture)
{
    simple_mem_interface mem(64);
    cache_config cfg = make_config(text);
    tex_cache cache("ParamTex", cfg, 0, 0, &mem, IN_L1T_MISS_QUEUE,
                    IN_SHADER_L1T_ROB);
    texture_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;
    mem_fetch *read = new_mf(0x2000, 4, false, TEXTURE_ACC_R, 1);
    CHECK_EQ(cache.access(read->get_addr(), read, 1, events), MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    cache.cycle();
    CHECK_EQ(mem.queue.size(), 1u);
    mem_fetch *req = mem.queue.front();
    mem.queue.pop_front();
    if (sector_texture) {
        for (unsigned sector = 0; sector < 3; ++sector) {
            mem_fetch *partial = new_child_mf(0x2000 + sector * SECTOR_SIZE,
                                              SECTOR_SIZE, false,
                                              TEXTURE_ACC_R, req, 2 + sector,
                                              sector_mask(sector),
                                              byte_mask_range(sector * SECTOR_SIZE,
                                                              SECTOR_SIZE));
            cache.fill(partial, 2 + sector);
            cache.cycle();
            CHECK_FALSE(cache.access_ready());
        }
        mem_fetch *final_resp = new_child_mf(0x2000 + 3 * SECTOR_SIZE,
                                             SECTOR_SIZE, false, TEXTURE_ACC_R,
                                             req, 5, sector_mask(3),
                                             byte_mask_range(3 * SECTOR_SIZE,
                                                             SECTOR_SIZE));
        cache.fill(final_resp, 5);
    } else {
        cache.fill(req, 2);
    }
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == read);
}

TEST(parameter_pairwise_smoke_matrix)
{
    const pairwise_smoke_case cases[] = {
        {"small_direct_mapped", "N:1:64:1,L:R:m:N:L,A:1:1,64", false, false, false},
        {"large_high_assoc", "N:256:128:16,L:R:m:N:X,A:64:8,128", false, false, false},
        {"sector_assoc", "S:4:128:4,L:B:m:N:L,S:4:2,8", false, false, true},
        {"streaming_on_fill", "N:8:64:2,L:R:s:N:L,A:8:2,16", false, false, false},
        {"explicit_narrow_port", "N:4:64:2,L:B:m:F:L,A:4:2,16:1,8", false, false, false},
        {"texture_fifo", "N:4:128:4,L:R:m:N:L,F:4:2,4:2", true, false, false},
        {"sector_texture_fifo", "S:4:128:4,L:R:m:N:L,T:4:2,4:2", true, true, false},
    };
    for (const pairwise_smoke_case &entry : cases) {
        if (entry.texture)
            run_texture_smoke(entry.text, entry.sector_texture);
        else
            run_data_smoke(entry.text, entry.sector_assoc);
    }
}

TEST(sequence_same_line_hit_miss_matrix)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:B:m:F:L,A:4:2,16");
    l1_cache cache("SeqLine", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;

    mem_fetch *cold = new_mf(0x1000, 4, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(cache.access(cold->get_addr(), cold, 1, events), MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    drain_one_level(cache, mem, 2);

    events.clear();
    mem_fetch *hit0 = new_mf(0x1000, 4, false, GLOBAL_ACC_R, 3);
    CHECK_EQ(cache.access(hit0->get_addr(), hit0, 3, events), HIT);
    CHECK_FALSE(has_event(events, READ_REQUEST_SENT));

    events.clear();
    mem_fetch *hit1 = new_mf(0x1010, 4, false, GLOBAL_ACC_R, 4);
    CHECK_EQ(cache.access(hit1->get_addr(), hit1, 4, events), HIT);
    CHECK_FALSE(has_event(events, READ_REQUEST_SENT));

    simple_mem_interface we_mem(64);
    cache_config we_cfg = make_config("N:4:64:2,L:E:m:N:L,A:4:2,16");
    l1_cache we("SeqWriteEvict", we_cfg, 0, 0, &we_mem, &allocator,
                IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_we(we, we_mem);
    events.clear();
    mem_fetch *warm = new_mf(0x2000, 4, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(we.access(warm->get_addr(), warm, 1, events), MISS);
    drain_one_level(we, we_mem, 2);

    events.clear();
    mem_fetch *evicting_write = new_mf(0x2000, 4, true, GLOBAL_ACC_W, 3);
    CHECK_EQ(we.access(evicting_write->get_addr(), evicting_write, 3, events),
             HIT);
    CHECK_TRUE(has_event(events, WRITE_REQUEST_SENT));
    drain_one_level(we, we_mem, 4);

    events.clear();
    mem_fetch *after_evict = new_mf(0x2000, 4, false, GLOBAL_ACC_R, 4);
    CHECK_EQ(we.access(after_evict->get_addr(), after_evict, 4, events), MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
}

TEST(sequence_same_set_and_full_cache_eviction)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:2:64:1,L:B:m:F:L,A:4:2,16");
    l1_cache cache("SeqFullCache", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;

    mem_fetch *set0 = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(cache.access(set0->get_addr(), set0, 1, events), MISS);
    drain_one_level(cache, mem, 2);
    events.clear();
    mem_fetch *set1 = new_mf(0x0040, 4, false, GLOBAL_ACC_R, 3);
    CHECK_EQ(cache.access(set1->get_addr(), set1, 3, events), MISS);
    drain_one_level(cache, mem, 4);

    events.clear();
    mem_fetch *hit_set0 = new_mf(0x0004, 4, false, GLOBAL_ACC_R, 5);
    CHECK_EQ(cache.access(hit_set0->get_addr(), hit_set0, 5, events), HIT);
    events.clear();
    mem_fetch *hit_set1 = new_mf(0x0044, 4, false, GLOBAL_ACC_R, 6);
    CHECK_EQ(cache.access(hit_set1->get_addr(), hit_set1, 6, events), HIT);
    drain_one_level(cache, mem, 7);

    events.clear();
    mem_fetch *conflict_set0 = new_mf(0x0080, 4, false, GLOBAL_ACC_R, 7);
    CHECK_EQ(cache.access(conflict_set0->get_addr(), conflict_set0, 7, events),
             MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    drain_one_level(cache, mem, 8);

    events.clear();
    mem_fetch *old_set0 = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 9);
    CHECK_EQ(cache.access(old_set0->get_addr(), old_set0, 9, events), MISS);
    events.clear();
    mem_fetch *still_set1 = new_mf(0x0040, 4, false, GLOBAL_ACC_R, 10);
    CHECK_EQ(cache.access(still_set1->get_addr(), still_set1, 10, events), HIT);
}

TEST(sequence_sector_partial_hit_miss_matrix)
{
    cache_config cfg = make_config("S:1:128:1,L:B:m:F:L,A:4:2,16");
    tag_array tags(cfg, 0, 0);
    tag_array_final_check_guard final_tags(tags);
    unsigned idx = 0;
    mem_fetch *sec0 = new_mf(0x3000, SECTOR_SIZE, false, GLOBAL_ACC_R, 1,
                             sector_mask(0),
                             byte_mask_range(0, SECTOR_SIZE));
    CHECK_EQ(tags.access(0x3000, 1, idx, sec0), MISS);
    tags.fill(idx, 2, sec0);
    CHECK_EQ(tags.probe(0x3004, idx, sec0, false), HIT);

    mem_fetch *sec1 = new_mf(0x3020, 4, false, GLOBAL_ACC_R, 3,
                             sector_mask(1), byte_mask_range(32, 4));
    CHECK_EQ(tags.probe(0x3020, idx, sec1, false), SECTOR_MISS);
    CHECK_EQ(tags.access(0x3020, 3, idx, sec1), SECTOR_MISS);
    tags.fill(idx, 4, sec1);
    CHECK_EQ(tags.probe(0x3020, idx, sec1, false), HIT);

    mem_fetch *conflict = new_mf(0x3080, SECTOR_SIZE, false, GLOBAL_ACC_R, 5,
                                 sector_mask(0),
                                 byte_mask_range(0, SECTOR_SIZE));
    CHECK_EQ(tags.access(0x3080, 5, idx, conflict), MISS);
    tags.fill(idx, 6, conflict);
    CHECK_EQ(tags.probe(0x3000, idx, sec0, false), MISS);
}

TEST(sequence_mshr_merge_and_pending_matrix)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,16");
    read_only_cache cache("SeqMshrMerge", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;

    mem_fetch *first = new_mf(0x4000, 4, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(cache.access(first->get_addr(), first, 1, events), MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    events.clear();
    mem_fetch *merged = new_mf(0x4010, 4, false, GLOBAL_ACC_R, 2);
    CHECK_EQ(cache.access(merged->get_addr(), merged, 2, events), MISS);
    CHECK_FALSE(has_event(events, READ_REQUEST_SENT));

    cache.cycle();
    CHECK_EQ(mem.queue.size(), 1u);
    mem_fetch *resp = mem.queue.front();
    mem.queue.pop_front();
    cache.fill(resp, 3);
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == first);
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == merged);
    CHECK_FALSE(cache.access_ready());

    simple_mem_interface fail_mem(64);
    cache_config fail_cfg = make_config("N:4:64:4,L:R:m:N:L,A:4:1,16");
    read_only_cache fail_cache("SeqMshrMergeFail", fail_cfg, 0, 0, &fail_mem,
                               IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_fail_cache(fail_cache, fail_mem);
    events.clear();
    mem_fetch *base = new_mf(0x5000, 4, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(fail_cache.access(base->get_addr(), base, 1, events), MISS);
    events.clear();
    mem_fetch *merge_fail = new_mf(0x5004, 4, false, GLOBAL_ACC_R, 2);
    CHECK_EQ(fail_cache.access(merge_fail->get_addr(), merge_fail, 2, events),
             RESERVATION_FAIL);
    CHECK_EQ(fail_cache.get_fail_stats(GLOBAL_ACC_R, MSHR_MERGE_ENRTY_FAIL),
             1ull);
}

TEST(sequence_texture_hit_reserved_order_matrix)
{
    simple_mem_interface mem(64);
    cache_config cfg = make_config("N:4:128:4,L:R:m:N:L,F:4:4,4:1");
    tex_cache cache("SeqTex", cfg, 0, 0, &mem, IN_L1T_MISS_QUEUE,
                    IN_SHADER_L1T_ROB);
    texture_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;

    mem_fetch *miss = new_mf(0x6000, 4, false, TEXTURE_ACC_R, 1);
    CHECK_EQ(cache.access(miss->get_addr(), miss, 1, events), MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    cache.cycle();
    CHECK_EQ(mem.queue.size(), 1u);
    mem_fetch *resp = mem.queue.front();
    mem.queue.pop_front();
    cache.fill(resp, 2);
    cache.cycle();
    CHECK_TRUE(cache.access_ready());

    events.clear();
    mem_fetch *hit_reserved = new_mf(0x6004, 4, false, TEXTURE_ACC_R, 3);
    CHECK_EQ(cache.access(hit_reserved->get_addr(), hit_reserved, 3, events),
             HIT_RESERVED);
    CHECK_FALSE(has_event(events, READ_REQUEST_SENT));

    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == miss);
    CHECK_FALSE(cache.access_ready());
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == hit_reserved);
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
    cache_config fermi64 = make_config("N:64:64:2,L:R:m:N:H,A:8:2,16");
    CHECK_TRUE(linear.set_index(0x4000) < linear.get_nset());
    CHECK_TRUE(xoring.set_index(0x4000) < xoring.get_nset());
    CHECK_TRUE(ipoly.set_index(0x4000) < ipoly.get_nset());
    CHECK_TRUE(custom.set_index(0x4000) < custom.get_nset());
    CHECK_TRUE(fermi.set_index(0x4000) < fermi.get_nset());
    CHECK_TRUE(fermi64.set_index(0x4000) < fermi64.get_nset());
    CHECK_TRUE(linear.set_index(0x1000) != xoring.set_index(0x1000) ||
               linear.set_index(0x2000) != xoring.set_index(0x2000));

    const set_index_golden golden[] = {
        {0x00000000ull, 0u, 0u, 0u, 0u, 0u, 0u},
        {0x00000040ull, 1u, 1u, 0u, 0u, 1u, 1u},
        {0x000007c0ull, 15u, 14u, 7u, 0u, 31u, 31u},
        {0x00001000ull, 0u, 4u, 5u, 0u, 0u, 32u},
        {0x00012340ull, 13u, 5u, 9u, 0u, 12u, 12u},
        {0x00020000ull, 0u, 0u, 7u, 0u, 8u, 8u},
        {0x00080000ull, 0u, 0u, 6u, 0u, 16u, 16u},
        {0x0abcdef0ull, 11u, 12u, 2u, 0u, 13u, 45u},
        {0xfffffffcull, 15u, 0u, 9u, 0u, 0u, 32u},
    };
    for (const set_index_golden &entry : golden) {
        CHECK_EQ(linear.set_index(entry.addr), entry.linear);
        CHECK_EQ(xoring.set_index(entry.addr), entry.xoring);
        CHECK_EQ(ipoly.set_index(entry.addr), entry.ipoly);
        CHECK_EQ(custom.set_index(entry.addr), entry.custom);
        CHECK_EQ(fermi.set_index(entry.addr), entry.fermi32);
        CHECK_EQ(fermi64.set_index(entry.addr), entry.fermi64);
    }
}

TEST(tag_fill_hit_reserved_and_sector_miss)
{
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,8");
    tag_array tags(cfg, 0, 0);
    tag_array_final_check_guard final_tags(tags);
    unsigned idx = 0;
    mem_fetch *read = new_mf(0x1000, 4, false, GLOBAL_ACC_R);

    CHECK_EQ(tags.probe(0x1000, idx, read, false), MISS);
    CHECK_EQ(tags.access(0x1000, 1, idx, read), MISS);
    CHECK_EQ(tags.probe(0x1000, idx, read, false), HIT_RESERVED);
    tags.fill(idx, 2, read);
    CHECK_EQ(tags.probe(0x1000, idx, read, false), HIT);

    cache_config sector_cfg = make_config("S:4:128:2,L:R:m:N:L,S:4:2,8");
    tag_array sector_tags(sector_cfg, 0, 0);
    tag_array_final_check_guard final_sector_tags(sector_tags);
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
    tag_array_final_check_guard final_lru(lru);
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
    tag_array_final_check_guard final_fifo(fifo);
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
    baseline_final_check_guard final_cache(cache, mem);
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
    tag_array_final_check_guard final_tags(tags);
    unsigned idx = 0;
    mem_fetch *a = new_mf(0x0000, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(tags.probe(0x0000, idx, a, false), MISS);
    tags.fill(0x0000, 1, a, false);
    CHECK_EQ(tags.probe(0x0000, idx, a, false), HIT);

    cache_config on_miss = make_config("N:1:64:1,L:R:m:N:L,A:4:2,8");
    tag_array one_way(on_miss, 0, 0);
    tag_array_final_check_guard final_one_way(one_way);
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
    mshr_final_check_guard final_mshr(mshr);
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
    mshr.mark_ready(0x2000, has_atomic);
    CHECK_TRUE(mshr.next_access() == r2);
    CHECK_FALSE(mshr.access_ready());

    mshr_table raw(4, 4);
    mshr_final_check_guard final_raw(raw);
    mem_fetch *w = new_mf(0x4000, 4, true, GLOBAL_ACC_W);
    mem_fetch *r = new_mf(0x4000, 4, false, GLOBAL_ACC_R);
    raw.add(0x4000, w);
    CHECK_FALSE(raw.is_read_after_write_pending(0x4000));
    raw.add(0x4000, r);
    CHECK_TRUE(raw.is_read_after_write_pending(0x4000));
    raw.mark_ready(0x4000, has_atomic);
    CHECK_TRUE(raw.next_access() == w);
    CHECK_TRUE(raw.next_access() == r);
    CHECK_FALSE(raw.access_ready());
}

TEST(read_only_flow_and_fail_stats)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,8");
    read_only_cache cache("RO", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
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
    baseline_final_check_guard final_fail_cache(fail_cache, small_mem);
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
    small_mem.max_queue_size = 64;
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
        baseline_final_check_guard final_cache(cache, mem);
        mem_fetch *first = new_mf(0x0000, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(first->get_addr(), first, 1, events), MISS);
        events.clear();
        mem_fetch *conflict = new_mf(0x0040, 4, false, GLOBAL_ACC_R);
        CHECK_EQ(cache.access(conflict->get_addr(), conflict, 2, events),
                 RESERVATION_FAIL);
        CHECK_EQ(cache.get_fail_stats(GLOBAL_ACC_R, LINE_PINNED_FAIL), 1ull);
    }

    {
        simple_mem_interface mem(64);
        cache_config cfg = make_config("N:4:64:4,L:B:m:F:L,A:1:4,8");
        l1_cache cache("MshrEntryFail", cfg, 0, 0, &mem, &allocator,
                       IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
        baseline_final_check_guard final_cache(cache, mem);
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
        baseline_final_check_guard final_cache(cache, mem);
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
    stats.inc_fail_stats(GLOBAL_ACC_R, LINE_PINNED_FAIL, 0);
    stats.inc_fail_stats(GLOBAL_ACC_R, HIT_RESPONSE_QUEUE_FULL, 0);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, LINE_ALLOC_FAIL), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, MISS_QUEUE_FULL), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, MSHR_ENRTY_FAIL), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, MSHR_MERGE_ENRTY_FAIL), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_W, MSHR_RW_PENDING), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, LINE_PINNED_FAIL), 1ull);
    CHECK_EQ(stats.get_fail_stats(GLOBAL_ACC_R, HIT_RESPONSE_QUEUE_FULL), 1ull);
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
        baseline_final_check_guard final_cache(cache, mem);
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
        baseline_final_check_guard final_cache(cache, mem);
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
        baseline_final_check_guard final_cache(cache, mem);
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
        baseline_final_check_guard final_cache(cache, mem);
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
        baseline_final_check_guard final_local_cache(local_cache, local_mem);
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
    baseline_final_check_guard final_nwa(nwa, nwa_mem);
    std::list<cache_event> events;
    mem_fetch *w0 = new_mf(0x1000, 4, true, GLOBAL_ACC_W);
    CHECK_EQ(nwa.access(w0->get_addr(), w0, 1, events), MISS);
    CHECK_TRUE(has_event(events, WRITE_REQUEST_SENT));
    CHECK_FALSE(has_event(events, READ_REQUEST_SENT));

    simple_mem_interface wa_mem(64);
    cache_config wa_cfg = make_config("N:4:64:2,L:B:m:W:L,A:8:4,16");
    l1_cache wa("WA", wa_cfg, 0, 0, &wa_mem, &allocator,
                IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_wa(wa, wa_mem);
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
    baseline_final_check_guard final_fow(fow, fow_mem);
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
    baseline_final_check_guard final_wb(wb, wb_mem);
    events.clear();
    mem_fetch *r = new_mf(0x0000, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(wb.access(r->get_addr(), r, 1, events), MISS);
    drain_one_level(wb, wb_mem, 2);
    events.clear();
    mem_fetch *w = new_mf(0x0000, 64, true, GLOBAL_ACC_W, 3,
                          sector_mask(0), byte_mask_range(0, 64));
    CHECK_EQ(wb.access(w->get_addr(), w, 3, events), HIT);
    drain_one_level(wb, wb_mem, 4);
    events.clear();
    mem_fetch *r2 = new_mf(0x0040, 4, false, GLOBAL_ACC_R);
    CHECK_EQ(wb.access(r2->get_addr(), r2, 4, events), MISS);
    CHECK_TRUE(has_event(events, WRITE_BACK_REQUEST_SENT));

    simple_mem_interface lazy_mem(64);
    cache_config lazy_cfg = make_config("S:2:128:1,L:B:m:L:L,A:8:4,16");
    l1_cache lazy("LazyPartial", lazy_cfg, 0, 0, &lazy_mem, &allocator,
                  IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_lazy(lazy, lazy_mem);
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
    baseline_final_check_guard final_full_lazy(full_lazy, full_mem);
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
    baseline_final_check_guard final_cache(cache, mem);
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

TEST(sector_assoc_pending_read_fill)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("S:4:128:4,L:B:m:N:L,S:4:2,8");
    l1_cache cache("SectorAssocPending", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;
    mem_fetch *read = new_mf(0x6008, 4, false, GLOBAL_ACC_R, 1,
                             sector_mask(0), byte_mask_range(8, 4));

    CHECK_EQ(cache.access(read->get_addr(), read, 1, events), MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    CHECK_EQ(read->get_addr(), 0x6000ull);
    CHECK_EQ(read->get_data_size(), (unsigned)SECTOR_SIZE);
    cache.cycle();
    CHECK_EQ(mem.queue.size(), 1u);
    CHECK_TRUE(mem.queue.front() == read);
    mem.queue.pop_front();

    for (unsigned sector = 0; sector < 3; ++sector) {
        mem_fetch *partial_resp = new_child_mf(0x6000 + sector * SECTOR_SIZE,
                                               SECTOR_SIZE, false,
                                               GLOBAL_ACC_R, read, 2 + sector,
                                               sector_mask(sector),
                                               byte_mask_range(sector * SECTOR_SIZE,
                                                               SECTOR_SIZE));
        cache.fill(partial_resp, 2 + sector);
        CHECK_FALSE(cache.access_ready());
    }

    mem_fetch *final_resp = new_child_mf(0x6000 + 3 * SECTOR_SIZE, SECTOR_SIZE,
                                         false, GLOBAL_ACC_R, read, 5,
                                         sector_mask(3),
                                         byte_mask_range(3 * SECTOR_SIZE,
                                                         SECTOR_SIZE));
    cache.fill(final_resp, 5);
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == read);
    CHECK_EQ(read->get_addr(), 0x6008ull);
    CHECK_EQ(read->get_data_size(), 4u);

    events.clear();
    mem_fetch *hit = new_mf(0x6008, 4, false, GLOBAL_ACC_R, 6,
                            sector_mask(0), byte_mask_range(8, 4));
    CHECK_EQ(cache.access(hit->get_addr(), hit, 6, events), HIT);
}

TEST(texture_pipeline_and_backpressure)
{
    simple_mem_interface mem(64);
    cache_config cfg = make_config("N:4:128:4,L:R:m:N:L,F:4:2,4:2");
    tex_cache cache("Tex", cfg, 0, 0, &mem, IN_L1T_MISS_QUEUE,
                    IN_SHADER_L1T_ROB);
    texture_final_check_guard final_cache(cache, mem);
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
    texture_final_check_guard final_bp(bp, bp_mem);
    mem_fetch *first = new_mf(0x2000, 4, false, TEXTURE_ACC_R);
    mem_fetch *second = new_mf(0x2080, 4, false, TEXTURE_ACC_R);
    events.clear();
    CHECK_EQ(bp.access(first->get_addr(), first, 1, events), MISS);
    CHECK_EQ(bp.access(second->get_addr(), second, 2, events), RESERVATION_FAIL);
    bp_mem.max_queue_size = 64;

    simple_mem_interface rf_mem(64);
    cache_config rf_cfg = make_config("N:4:128:4,L:R:m:N:L,F:4:4,4:1");
    tex_cache rf("TexResultFIFO", rf_cfg, 0, 0, &rf_mem, IN_L1T_MISS_QUEUE,
                 IN_SHADER_L1T_ROB);
    texture_final_check_guard final_rf(rf, rf_mem);
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

TEST(texture_sector_pending_and_return_order)
{
    simple_mem_interface sector_mem(64);
    cache_config sector_cfg = make_config("S:4:128:4,L:R:m:N:L,T:4:2,4:2");
    tex_cache sector_cache("TexSector", sector_cfg, 0, 0, &sector_mem,
                           IN_L1T_MISS_QUEUE, IN_SHADER_L1T_ROB);
    texture_final_check_guard final_sector_cache(sector_cache, sector_mem);
    std::list<cache_event> events;
    mem_fetch *sector_req = new_mf(0x4000, 4, false, TEXTURE_ACC_R, 1);
    CHECK_EQ(sector_cache.access(sector_req->get_addr(), sector_req, 1, events),
             MISS);
    CHECK_TRUE(has_event(events, READ_REQUEST_SENT));
    sector_cache.cycle();
    CHECK_EQ(sector_mem.queue.size(), 1u);
    CHECK_TRUE(sector_mem.queue.front() == sector_req);
    sector_mem.queue.pop_front();

    for (unsigned sector = 0; sector < 3; ++sector) {
        mem_fetch *partial_resp = new_child_mf(0x4000 + sector * SECTOR_SIZE,
                                               SECTOR_SIZE, false,
                                               TEXTURE_ACC_R, sector_req,
                                               2 + sector, sector_mask(sector),
                                               byte_mask_range(sector * SECTOR_SIZE,
                                                               SECTOR_SIZE));
        sector_cache.fill(partial_resp, 2 + sector);
        sector_cache.cycle();
        CHECK_FALSE(sector_cache.access_ready());
    }
    mem_fetch *final_resp = new_child_mf(0x4000 + 3 * SECTOR_SIZE, SECTOR_SIZE,
                                         false, TEXTURE_ACC_R, sector_req, 5,
                                         sector_mask(3),
                                         byte_mask_range(3 * SECTOR_SIZE,
                                                         SECTOR_SIZE));
    sector_cache.fill(final_resp, 5);
    sector_cache.cycle();
    CHECK_TRUE(sector_cache.access_ready());
    CHECK_TRUE(sector_cache.next_access() == sector_req);

    simple_mem_interface order_mem(64);
    cache_config order_cfg = make_config("N:8:128:4,L:R:m:N:L,F:4:4,4:4");
    tex_cache order_cache("TexOrder", order_cfg, 0, 0, &order_mem,
                          IN_L1T_MISS_QUEUE, IN_SHADER_L1T_ROB);
    texture_final_check_guard final_order_cache(order_cache, order_mem);
    events.clear();
    mem_fetch *first = new_mf(0x5000, 4, false, TEXTURE_ACC_R, 10);
    mem_fetch *second = new_mf(0x5080, 4, false, TEXTURE_ACC_R, 11);
    CHECK_EQ(order_cache.access(first->get_addr(), first, 10, events), MISS);
    events.clear();
    CHECK_EQ(order_cache.access(second->get_addr(), second, 11, events), MISS);
    order_cache.cycle();
    CHECK_EQ(order_mem.queue.size(), 1u);
    CHECK_TRUE(order_mem.queue.front() == first);
    order_mem.queue.pop_front();
    order_cache.cycle();
    CHECK_EQ(order_mem.queue.size(), 1u);
    CHECK_TRUE(order_mem.queue.front() == second);
    order_mem.queue.pop_front();

    order_cache.fill(second, 12);
    order_cache.cycle();
    CHECK_FALSE(order_cache.access_ready());
    order_cache.fill(first, 13);
    order_cache.cycle();
    CHECK_TRUE(order_cache.access_ready());
    CHECK_TRUE(order_cache.next_access() == first);
    order_cache.cycle();
    CHECK_TRUE(order_cache.access_ready());
    CHECK_TRUE(order_cache.next_access() == second);
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
    baseline_final_check_guard final_cache(cache, mem);
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

struct property_result {
    cache_sub_stats stats;
    unsigned accepted;
};

struct mixed_property_result {
    cache_sub_stats stats;
    cache_sub_stats_pw stats_pw;
    unsigned expected_accesses;
    unsigned expected_misses;
    bool ok;
};

static property_result run_read_only_seed(unsigned seed, unsigned iterations)
{
    simple_mem_interface mem(2048);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:64:64:4,L:R:m:N:L,A:64:8,128");
    read_only_cache cache("SeedProp", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    std::mt19937 rng(seed);
    unsigned accepted = 0;
    for (unsigned i = 0; i < iterations; ++i) {
        new_addr_type addr = (rng() % 16384) & ~0x3ull;
        mem_fetch *mf = new_mf(addr, 4, false, GLOBAL_ACC_R, i);
        std::list<cache_event> events;
        cache_request_status status = cache.access(mf->get_addr(), mf, i, events);
        if (status != RESERVATION_FAIL)
            accepted++;
        drain_one_level(cache, mem, i);
    }

    cache_sub_stats stats;
    cache.get_sub_stats(stats);
    property_result result;
    result.stats = stats;
    result.accepted = accepted;
    return result;
}

TEST(multi_seed_differential_property_trace)
{
    const unsigned seeds[] = {0xCACE2026u, 0x12345678u, 0xA5A5A5A5u,
                              0x0000BEEFu, 0xDEADBEEFu};
    for (unsigned seed : seeds) {
        property_result first = run_read_only_seed(seed, 512);
        property_result second = run_read_only_seed(seed, 512);
        CHECK_EQ(first.stats.accesses, (unsigned long long)first.accepted);
        CHECK_EQ(second.stats.accesses, (unsigned long long)second.accepted);
        CHECK_EQ(first.stats.res_fails, 0ull);
        CHECK_EQ(second.stats.res_fails, 0ull);
        CHECK_TRUE(first.stats.misses <= first.stats.accesses);
        CHECK_EQ(first.stats.accesses, second.stats.accesses);
        CHECK_EQ(first.stats.misses, second.stats.misses);
        CHECK_EQ(first.stats.pending_hits, second.stats.pending_hits);
        CHECK_EQ(first.stats.res_fails, second.stats.res_fails);
        CHECK_TRUE(first.stats.accesses > 0);
        CHECK_TRUE(first.stats.misses > 0);
    }
}

static mixed_property_result run_mixed_seed(unsigned seed, unsigned iterations)
{
    simple_mem_interface mem(4096);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:128:64:4,L:B:m:N:L,A:128:8,256");
    l1_cache cache("MixedProp", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);
    std::mt19937 rng(seed);
    bool resident[64] = {};
    unsigned expected_accesses = 0;
    unsigned expected_misses = 0;
    bool ok = true;

    for (unsigned i = 0; i < iterations; ++i) {
        unsigned line = rng() % 64;
        unsigned byte = (rng() % 16) * 4;
        new_addr_type addr = line * 64ull + byte;
        bool wr = resident[line] && ((rng() & 1u) != 0);
        mem_access_type type = wr ? GLOBAL_ACC_W : GLOBAL_ACC_R;
        mem_fetch *mf = new_mf(addr, 4, wr, type, i,
                               sector_mask(byte / SECTOR_SIZE),
                               byte_mask_range(byte, 4));
        std::list<cache_event> events;
        cache_request_status status = cache.access(mf->get_addr(), mf, i, events);
        expected_accesses++;
        if (resident[line]) {
            ok = ok && (status == HIT);
            drain_one_level(cache, mem, i + 1);
        } else {
            ok = ok && !wr;
            ok = ok && (status == MISS);
            ok = ok && has_event(events, READ_REQUEST_SENT);
            expected_misses++;
            resident[line] = true;
            drain_one_level(cache, mem, i + 1);
        }
    }

    cache_sub_stats stats;
    cache_sub_stats_pw stats_pw;
    cache.get_sub_stats(stats);
    cache.get_sub_stats_pw(stats_pw);
    mixed_property_result result;
    result.stats = stats;
    result.stats_pw = stats_pw;
    result.expected_accesses = expected_accesses;
    result.expected_misses = expected_misses;
    result.ok = ok;
    return result;
}

TEST(mixed_read_write_differential_property_trace)
{
    const unsigned seeds[] = {0xFACE0001u, 0xFACE0002u, 0xFACE0003u,
                              0xFACE0004u};
    for (unsigned seed : seeds) {
        mixed_property_result first = run_mixed_seed(seed, 1024);
        mixed_property_result second = run_mixed_seed(seed, 1024);
        CHECK_TRUE(first.ok);
        CHECK_TRUE(second.ok);
        CHECK_EQ(first.stats.accesses,
                 (unsigned long long)first.expected_accesses);
        CHECK_EQ(first.stats.misses,
                 (unsigned long long)first.expected_misses);
        CHECK_EQ(first.stats.res_fails, 0ull);
        CHECK_TRUE(first.expected_misses > 0);
        CHECK_TRUE(first.stats_pw.write_hits > 0);
        CHECK_TRUE(first.stats_pw.read_hits > 0);
        CHECK_EQ(first.stats.accesses, second.stats.accesses);
        CHECK_EQ(first.stats.misses, second.stats.misses);
        CHECK_EQ(first.stats_pw.write_hits, second.stats_pw.write_hits);
        CHECK_EQ(first.stats_pw.read_hits, second.stats_pw.read_hits);
        CHECK_EQ(first.stats.res_fails, second.stats.res_fails);
    }
}

TEST(port_timing_visibility)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:B:m:F:L,A:4:2,16:1,8");
    l1_cache cache("Ports", cfg, 0, 0, &mem, &allocator, IN_L1D_MISS_QUEUE,
                   &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);
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

TEST(hitlat_explicit_old_mode_compatibility)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,16");
    cfg.set_defer_hit_response(false);
    read_only_cache cache("HitLatCompat", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    fill_read_only_line(cache, mem, 0x1000, 1);

    std::list<cache_event> events;
    mem_fetch *hit = new_mf(0x1000, 4, false, GLOBAL_ACC_R, 3);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 3, events), HIT);
    CHECK_FALSE(cache.access_ready());
    final_check(cache, mem, 4);
}

TEST(hitlat_read_only_deferred_ready_latency)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,16:1,8");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(4);
    read_only_cache cache("HitLatRO", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    fill_read_only_line(cache, mem, 0x1000, 1);

    std::list<cache_event> events;
    mem_fetch *hit = new_mf(0x1000, 16, false, GLOBAL_ACC_R, 3);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 3, events), HIT);
    CHECK_FALSE(cache.access_ready());
    cache.cycle();
    CHECK_FALSE(cache.access_ready());
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == hit);
    CHECK_FALSE(cache.access_ready());
}

TEST(hitlat_data_read_and_write_deferred_ready)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:B:m:F:L,A:4:2,16:1,8");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(4);
    l1_cache cache("HitLatData", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);

    std::list<cache_event> events;
    mem_fetch *warm = new_mf(0x2000, 4, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(cache.access(warm->get_addr(), warm, 1, events), MISS);
    drain_one_level(cache, mem, 2);

    events.clear();
    mem_fetch *read_hit = new_mf(0x2000, 16, false, GLOBAL_ACC_R, 3);
    CHECK_EQ(cache.access(read_hit->get_addr(), read_hit, 3, events), HIT);
    CHECK_FALSE(cache.access_ready());
    cache.cycle();
    CHECK_FALSE(cache.access_ready());
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == read_hit);

    events.clear();
    mem_fetch *write_hit = new_mf(0x2000, 8, true, GLOBAL_ACC_W, 6,
                                  sector_mask(0), byte_mask_range(0, 8));
    CHECK_EQ(cache.access(write_hit->get_addr(), write_hit, 6, events), HIT);
    CHECK_FALSE(has_event(events, WRITE_REQUEST_SENT));
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == write_hit);

    simple_mem_interface wt_mem(64);
    cache_config wt_cfg = make_config("N:4:64:2,L:T:m:N:L,A:4:2,16");
    wt_cfg.set_defer_hit_response(true);
    wt_cfg.set_hit_response_queue_size(4);
    l1_cache wt_cache("HitLatWT", wt_cfg, 0, 0, &wt_mem, &allocator,
                      IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_wt_cache(wt_cache, wt_mem);
    events.clear();
    mem_fetch *wt_warm = new_mf(0x2400, 4, false, GLOBAL_ACC_R, 8);
    CHECK_EQ(wt_cache.access(wt_warm->get_addr(), wt_warm, 8, events), MISS);
    drain_one_level(wt_cache, wt_mem, 9);
    events.clear();
    mem_fetch *wt_write = new_mf(0x2400, 4, true, GLOBAL_ACC_W, 10);
    CHECK_EQ(wt_cache.access(wt_write->get_addr(), wt_write, 10, events), HIT);
    CHECK_TRUE(has_event(events, WRITE_REQUEST_SENT));
    wt_cache.cycle();
    CHECK_TRUE(wt_cache.access_ready());
    CHECK_TRUE(wt_cache.next_access() == wt_write);
}

TEST(hitlat_same_cycle_hit_ready_precedes_miss_ready)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:1:64:2,L:R:m:N:L,A:4:2,16");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(4);
    read_only_cache cache("HitLatReadyOrder", cfg, 0, 0, &mem,
                          IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    fill_read_only_line(cache, mem, 0x0000, 1);

    std::list<cache_event> events;
    mem_fetch *miss = new_mf(0x0040, 4, false, GLOBAL_ACC_R, 3);
    CHECK_EQ(cache.access(miss->get_addr(), miss, 3, events), MISS);
    cache.cycle();
    CHECK_EQ(mem.queue.size(), 1u);
    mem_fetch *resp = mem.queue.front();
    mem.queue.pop_front();

    events.clear();
    mem_fetch *hit = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 4);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 4, events), HIT);
    cache.fill(resp, 4);
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == hit);
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == miss);
    CHECK_FALSE(cache.access_ready());
}

TEST(hitlat_stats_port_and_exactly_once_trace)
{
    simple_mem_interface mem(256);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:8:64:2,L:B:m:N:L,A:8:4,32:1,8");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(8);
    l1_cache cache("HitLatStats", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);

    std::vector<mem_fetch *> accepted;
    std::vector<mem_fetch *> returned;
    const new_addr_type trace[] = {0x0000, 0x0040, 0x0000, 0x0008,
                                   0x0040, 0x0080, 0x0084, 0x0000};
    for (unsigned i = 0; i < sizeof(trace) / sizeof(trace[0]); ++i) {
        std::list<cache_event> events;
        mem_fetch *mf = new_mf(trace[i], 16, false, GLOBAL_ACC_R, i);
        cache_request_status status = cache.access(mf->get_addr(), mf, i, events);
        if (status != RESERVATION_FAIL)
            accepted.push_back(mf);

        for (unsigned step = 0; step < 8; ++step) {
            cache.cycle();
            while (!mem.queue.empty()) {
                mem_fetch *resp = mem.queue.front();
                mem.queue.pop_front();
                cache.fill(resp, i + step + 1);
            }
            while (cache.access_ready())
                returned.push_back(cache.next_access());
        }
    }

    CHECK_EQ(returned.size(), accepted.size());
    for (unsigned i = 0; i < accepted.size(); ++i)
        CHECK_TRUE(returned[i] == accepted[i]);

    cache_sub_stats css;
    cache.get_sub_stats(css);
    CHECK_EQ(css.accesses, (unsigned long long)accepted.size());
    CHECK_TRUE(css.misses > 0);
    CHECK_TRUE(css.data_port_busy_cycles > 0);
}

TEST(hitlat_queue_backpressure_and_recovery)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,16");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(1);
    read_only_cache cache("HitLatQueue", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    fill_read_only_line(cache, mem, 0x3000, 1);

    std::list<cache_event> events;
    mem_fetch *first = new_mf(0x3000, 4, false, GLOBAL_ACC_R, 3);
    CHECK_EQ(cache.access(first->get_addr(), first, 3, events), HIT);
    events.clear();
    mem_fetch *blocked = new_mf(0x3004, 4, false, GLOBAL_ACC_R, 4);
    CHECK_EQ(cache.access(blocked->get_addr(), blocked, 4, events),
             RESERVATION_FAIL);
    CHECK_EQ(cache.get_fail_stats(GLOBAL_ACC_R, HIT_RESPONSE_QUEUE_FULL), 1ull);

    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == first);
    events.clear();
    mem_fetch *retry = new_mf(0x3004, 4, false, GLOBAL_ACC_R, 5);
    CHECK_EQ(cache.access(retry->get_addr(), retry, 5, events), HIT);
}

TEST(hitlat_pending_hit_pins_line_until_next_access)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:1:64:1,L:R:m:N:L,A:4:2,16");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(4);
    read_only_cache cache("HitLatPin", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    fill_read_only_line(cache, mem, 0x0000, 1);

    std::list<cache_event> events;
    mem_fetch *hit = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 3);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 3, events), HIT);
    events.clear();
    mem_fetch *conflict = new_mf(0x0040, 4, false, GLOBAL_ACC_R, 4);
    CHECK_EQ(cache.access(conflict->get_addr(), conflict, 4, events),
             RESERVATION_FAIL);
    CHECK_EQ(cache.get_fail_stats(GLOBAL_ACC_R, LINE_PINNED_FAIL), 1ull);

    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == hit);
    events.clear();
    mem_fetch *after_unpin = new_mf(0x0040, 4, false, GLOBAL_ACC_R, 5);
    CHECK_EQ(cache.access(after_unpin->get_addr(), after_unpin, 5, events),
             MISS);
}

TEST(hitlat_multiple_hits_refcount_decrements_one_by_one)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:1:64:1,L:R:m:N:L,A:4:2,16");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(4);
    read_only_cache cache("HitLatMultiPin", cfg, 0, 0, &mem,
                          IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    fill_read_only_line(cache, mem, 0x0000, 1);

    std::list<cache_event> events;
    mem_fetch *hit0 = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 3);
    mem_fetch *hit1 = new_mf(0x0004, 4, false, GLOBAL_ACC_R, 4);
    CHECK_EQ(cache.access(hit0->get_addr(), hit0, 3, events), HIT);
    events.clear();
    CHECK_EQ(cache.access(hit1->get_addr(), hit1, 4, events), HIT);
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == hit0);

    events.clear();
    mem_fetch *still_pinned = new_mf(0x0040, 4, false, GLOBAL_ACC_R, 5);
    CHECK_EQ(cache.access(still_pinned->get_addr(), still_pinned, 5, events),
             RESERVATION_FAIL);
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == hit1);

    events.clear();
    mem_fetch *after_all = new_mf(0x0040, 4, false, GLOBAL_ACC_R, 6);
    CHECK_EQ(cache.access(after_all->get_addr(), after_all, 6, events), MISS);
}

TEST(hitlat_mshr_merge_pins_each_ready_response)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:1:64:1,L:R:m:N:L,A:4:2,16");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(4);
    read_only_cache cache("HitLatMshrPin", cfg, 0, 0, &mem,
                          IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;

    mem_fetch *first = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(cache.access(first->get_addr(), first, 1, events), MISS);
    events.clear();
    mem_fetch *merged = new_mf(0x0004, 4, false, GLOBAL_ACC_R, 2);
    CHECK_EQ(cache.access(merged->get_addr(), merged, 2, events), MISS);
    cache.cycle();
    CHECK_EQ(mem.queue.size(), 1u);
    mem_fetch *resp = mem.queue.front();
    mem.queue.pop_front();
    cache.fill(resp, 3);

    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == first);
    events.clear();
    mem_fetch *conflict = new_mf(0x0040, 4, false, GLOBAL_ACC_R, 4);
    CHECK_EQ(cache.access(conflict->get_addr(), conflict, 4, events),
             RESERVATION_FAIL);

    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == merged);
    events.clear();
    mem_fetch *after_all = new_mf(0x0040, 4, false, GLOBAL_ACC_R, 5);
    CHECK_EQ(cache.access(after_all->get_addr(), after_all, 5, events), MISS);
}

TEST(hitlat_sector_line_level_pin)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("S:1:128:1,L:B:m:N:L,A:4:2,16");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(4);
    l1_cache cache("HitLatSectorPin", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;

    mem_fetch *warm = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 1,
                             sector_mask(0), byte_mask_range(0, 4));
    CHECK_EQ(cache.access(warm->get_addr(), warm, 1, events), MISS);
    drain_one_level(cache, mem, 2);

    events.clear();
    mem_fetch *hit = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 3,
                            sector_mask(0), byte_mask_range(0, 4));
    CHECK_EQ(cache.access(hit->get_addr(), hit, 3, events), HIT);
    events.clear();
    mem_fetch *conflict = new_mf(0x0080, 4, false, GLOBAL_ACC_R, 4,
                                 sector_mask(0), byte_mask_range(0, 4));
    CHECK_EQ(cache.access(conflict->get_addr(), conflict, 4, events),
             RESERVATION_FAIL);
    CHECK_EQ(cache.get_fail_stats(GLOBAL_ACC_R, LINE_PINNED_FAIL), 1ull);
}

TEST(hitlat_write_evict_invalid_line_stays_pinned)
{
    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:1:64:1,L:E:m:N:L,A:4:2,16");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(4);
    l1_cache cache("HitLatWriteEvictPin", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
    baseline_final_check_guard final_cache(cache, mem);
    std::list<cache_event> events;

    mem_fetch *warm = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 1);
    CHECK_EQ(cache.access(warm->get_addr(), warm, 1, events), MISS);
    drain_one_level(cache, mem, 2);

    events.clear();
    mem_fetch *write = new_mf(0x0000, 4, true, GLOBAL_ACC_W, 3);
    CHECK_EQ(cache.access(write->get_addr(), write, 3, events), HIT);
    CHECK_TRUE(has_event(events, WRITE_REQUEST_SENT));
    events.clear();
    mem_fetch *same_line_read = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 4);
    CHECK_EQ(cache.access(same_line_read->get_addr(), same_line_read, 4, events),
             RESERVATION_FAIL);
    CHECK_EQ(cache.get_fail_stats(GLOBAL_ACC_R, LINE_PINNED_FAIL), 1ull);

    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == write);
    events.clear();
    mem_fetch *after_unpin = new_mf(0x0000, 4, false, GLOBAL_ACC_R, 5);
    CHECK_EQ(cache.access(after_unpin->get_addr(), after_unpin, 5, events),
             MISS);
}

TEST(hitlat_datastore_timing_token_only)
{
    DataStore store;
    uint8_t before[4] = {1, 2, 3, 4};
    uint8_t after[4] = {9, 8, 7, 6};
    store.write(0x1000, before, 4);

    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,16");
    cfg.set_defer_hit_response(true);
    cfg.set_hit_response_queue_size(4);
    read_only_cache cache("HitLatDataStore", cfg, 0, 0, &mem,
                          IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);
    fill_read_only_line(cache, mem, 0x1000, 1);

    std::list<cache_event> events;
    mem_fetch *hit = new_mf(0x1000, 4, false, GLOBAL_ACC_R, 3);
    std::vector<uint8_t> accepted_snapshot = store.read(0x1000, 4);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 3, events), HIT);
    store.write(0x1000, after, 4);
    std::vector<uint8_t> current_data = store.read(0x1000, 4);
    CHECK_EQ(accepted_snapshot[0], 1u);
    CHECK_EQ(current_data[0], 9u);
    cache.cycle();
    CHECK_TRUE(cache.access_ready());
    CHECK_TRUE(cache.next_access() == hit);
}

TEST(final_check_baseline_cache_returns_to_initial_state)
{
    simple_mem_interface mem(64);
    gpgpu_sim gpu;
    cache_config cfg = make_config("N:4:64:2,L:R:m:N:L,A:4:2,16:1,8");
    read_only_cache cache("FinalBaseline", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                          OTHER_GPU_CACHE, &gpu);
    baseline_final_check_guard final_cache(cache, mem);

    fill_read_only_line(cache, mem, 0x1000, 1);
    CHECK_FALSE(cache.final_state_clean());

    std::list<cache_event> events;
    mem_fetch *hit = new_mf(0x1000, 4, false, GLOBAL_ACC_R, 4);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 4, events), HIT);
    final_check(cache, mem, 5);
}

TEST(final_check_texture_cache_returns_to_initial_state)
{
    simple_mem_interface mem(64);
    cache_config cfg = make_config("N:4:128:4,L:R:m:N:L,F:4:2,4:2");
    tex_cache cache("FinalTexture", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE,
                    IN_SHADER_FETCHED);
    texture_final_check_guard final_cache(cache, mem);

    std::list<cache_event> events;
    mem_fetch *miss = new_mf(0x2000, 16, false, TEXTURE_ACC_R, 1);
    CHECK_EQ(cache.access(miss->get_addr(), miss, 1, events), MISS);
    drain_texture(cache, mem, 2);
    CHECK_FALSE(cache.final_state_clean());

    events.clear();
    mem_fetch *hit = new_mf(0x2000, 16, false, TEXTURE_ACC_R, 4);
    CHECK_EQ(cache.access(hit->get_addr(), hit, 4, events), HIT_RESERVED);
    final_check(cache, mem, 5);
}

int main()
{
    printf("\n========== GPGPU-Sim Cache Deep Whitebox Test Suite ==========\n\n");

    RUN_TEST(config_sector_and_streaming);
    RUN_TEST(status_string_tables);
    RUN_TEST(parameter_single_axis_matrix);
    RUN_TEST(parameter_pairwise_smoke_matrix);
    RUN_TEST(sequence_same_line_hit_miss_matrix);
    RUN_TEST(sequence_same_set_and_full_cache_eviction);
    RUN_TEST(sequence_sector_partial_hit_miss_matrix);
    RUN_TEST(sequence_mshr_merge_and_pending_matrix);
    RUN_TEST(sequence_texture_hit_reserved_order_matrix);
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
    RUN_TEST(sector_assoc_pending_read_fill);
    RUN_TEST(texture_pipeline_and_backpressure);
    RUN_TEST(texture_sector_pending_and_return_order);
    RUN_TEST(stats_datastore_property_trace);
    RUN_TEST(multi_seed_differential_property_trace);
    RUN_TEST(mixed_read_write_differential_property_trace);
    RUN_TEST(port_timing_visibility);
    RUN_TEST(hitlat_explicit_old_mode_compatibility);
    RUN_TEST(hitlat_read_only_deferred_ready_latency);
    RUN_TEST(hitlat_data_read_and_write_deferred_ready);
    RUN_TEST(hitlat_same_cycle_hit_ready_precedes_miss_ready);
    RUN_TEST(hitlat_stats_port_and_exactly_once_trace);
    RUN_TEST(hitlat_queue_backpressure_and_recovery);
    RUN_TEST(hitlat_pending_hit_pins_line_until_next_access);
    RUN_TEST(hitlat_multiple_hits_refcount_decrements_one_by_one);
    RUN_TEST(hitlat_mshr_merge_pins_each_ready_response);
    RUN_TEST(hitlat_sector_line_level_pin);
    RUN_TEST(hitlat_write_evict_invalid_line_stays_pinned);
    RUN_TEST(hitlat_datastore_timing_token_only);
    RUN_TEST(final_check_baseline_cache_returns_to_initial_state);
    RUN_TEST(final_check_texture_cache_returns_to_initial_state);

    printf("\n========== Results: %d/%d tests passed ==========\n",
           tests_passed, tests_run);
    if (tests_failed)
        printf("========== Failures: %d ==========\n", tests_failed);

    return tests_failed == 0 && tests_passed == tests_run ? 0 : 1;
}
