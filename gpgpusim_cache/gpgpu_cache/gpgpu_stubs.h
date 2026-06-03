// =============================================================================
// Minimal stub layer for compiling GPGPU-Sim cache code standalone.
// Provides typedefs, classes, and function declarations that gpu-cache.h/cc
// depends on but that are part of the larger GPGPU-Sim simulator.
//
// The gpu-cache.h and gpu-cache.cc files themselves are UNMODIFIED from
// GPGPU-Sim. This stub file + minor adjustments to included headers provide
// the minimal environment needed to compile them.
// =============================================================================

#ifndef GPGPU_STUBS_H
#define GPGPU_STUBS_H

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <bitset>
#include <string>

// ---- Basic types (from abstract_hardware_model.h) ----
typedef unsigned long long new_addr_type;
#define SECTOR_SIZE 32
#define SECTOR_CHUNCK_SIZE 4
#define MAX_MEMORY_ACCESS_SIZE 128
#define MAX_DEFAULT_CACHE_SIZE_MULTIBLIER 4

typedef std::bitset<MAX_MEMORY_ACCESS_SIZE> mem_access_byte_mask_t;
typedef std::bitset<SECTOR_CHUNCK_SIZE> mem_access_sector_mask_t;

// ---- LOGB2 macro (from gpu-misc.h) ----
#define LOGB2(x) ({\
    unsigned _log = 0;\
    unsigned _x = (x);\
    while (_x >>= 1) _log++;\
    _log;\
})

// ---- Memory access status (from mem_fetch_status.tup) ----
enum mem_fetch_status {
    IN_SHADER_FETCHED,
    IN_L1I_MISS_QUEUE,
    IN_L1D_MISS_QUEUE,
    IN_L1C_MISS_QUEUE,
    IN_L1T_MISS_QUEUE,
    IN_SHADER_L1T_ROB,
    IN_PARTITION_ICNT_TO_L2_QUEUE,
    IN_PARTITION_ROP_DELAY,
    IN_PARTITION_L2_TO_ICNT_QUEUE,
    IN_PARTITION_L2_FILL_QUEUE,
    IN_PARTITION_L2_TO_DRAM_QUEUE,
    NUM_MEM_FETCH_STATUS
};

// ---- Function cache type (from abstract_hardware_model.h) ----
enum FuncCache {
    FuncCachePreferNone = 0,
    FuncCachePreferShared,
    FuncCachePreferL1,
    FuncCachePreferEqual
};

// ---- Memory access type (from abstract_hardware_model.h) ----
enum mem_access_type {
    GLOBAL_ACC_R = 0,
    LOCAL_ACC_R,
    CONST_ACC_R,
    TEXTURE_ACC_R,
    GLOBAL_ACC_W,
    LOCAL_ACC_W,
    L1_WRBK_ACC,
    L2_WRBK_ACC,
    INST_ACC_R,
    L1_WR_ALLOC_R,
    L2_WR_ALLOC_R,
    NUM_MEM_ACCESS_TYPE
};

// ---- Active mask (from abstract_hardware_model.h) ----
typedef std::bitset<64> active_mask_t;

// ---- Memory access descriptor ----
struct mem_access_t {
    mem_access_t()
        : m_type(GLOBAL_ACC_R), m_addr(0), m_size(0), m_is_write(false) {
        m_sector_mask.set(0);  // default: at least bit 0 valid
        m_byte_mask.set();     // default: all bytes valid
    }
    mem_access_t(enum mem_access_type type, new_addr_type addr, unsigned size,
                 bool wr, const active_mask_t &mask,
                 const mem_access_byte_mask_t &byte_mask,
                 const mem_access_sector_mask_t &sector_mask,
                 unsigned ctx = 0)
        : m_type(type), m_addr(addr), m_size(size), m_access_mask(mask),
          m_byte_mask(byte_mask), m_sector_mask(sector_mask), m_is_write(wr) {
        if (m_sector_mask.count() == 0) m_sector_mask.set(0);
    }

    enum mem_access_type get_type() const { return m_type; }
    new_addr_type get_addr() const { return m_addr; }
    unsigned get_size() const { return m_size; }
    bool is_write() const { return m_is_write; }
    const active_mask_t &get_warp_mask() const { return m_access_mask; }
    const mem_access_byte_mask_t &get_byte_mask() const { return m_byte_mask; }
    const mem_access_sector_mask_t &get_sector_mask() const { return m_sector_mask; }

    enum mem_access_type m_type;
    new_addr_type m_addr;
    unsigned m_size;
    active_mask_t m_access_mask;
    mem_access_byte_mask_t m_byte_mask;
    mem_access_sector_mask_t m_sector_mask;
    bool m_is_write;
};

// ---- Forward declarations ----
class warp_inst_t;
class gpgpu_sim;
struct memory_config;

// ---- Memory config stub (used by mem_fetch) ----
struct memory_config {
    unsigned rop_latency = 160;
    bool m_L2_texure_only = false;
    bool m_L2_config_disabled = false;
    bool SST_mode = false;
};

// ---- Memory fetch interface (from mem_fetch.h) ----
class mem_fetch_interface {
public:
    virtual ~mem_fetch_interface() {}
    virtual bool full(unsigned size, bool write) const = 0;
    virtual void push(class mem_fetch *mf) = 0;
};

// ---- Memory fetch allocator (from mem_fetch.h) ----
class mem_fetch_allocator {
public:
    virtual ~mem_fetch_allocator() {}
    virtual class mem_fetch *alloc(new_addr_type addr, mem_access_type type,
                                    unsigned size, bool wr,
                                    unsigned long long cycle,
                                    unsigned long long streamID) const = 0;
    virtual class mem_fetch *alloc(new_addr_type addr, mem_access_type type,
                                    const active_mask_t &active_mask,
                                    const mem_access_byte_mask_t &byte_mask,
                                    const mem_access_sector_mask_t &sector_mask,
                                    unsigned size, bool wr,
                                    unsigned long long cycle,
                                    unsigned wid, unsigned sid, unsigned tpc,
                                    class mem_fetch *original_mf,
                                    unsigned long long streamID) const = 0;
};

// ---- GPGPU-Sim stub (used as gpgpu_sim* m_gpu in baseline_cache) ----
class gpgpu_sim {
public:
    unsigned long long gpu_sim_cycle;
    unsigned long long gpu_tot_sim_cycle;
    unsigned gpgpu_ctx;
    gpgpu_sim() : gpu_sim_cycle(0), gpu_tot_sim_cycle(0), gpgpu_ctx(0) {}
};

// ---- Stub for shader core config (used by mem_fetch for get_mem_config) ----
struct shader_core_config {};

// ---- Stub for TLX address (used by mem_fetch for set_chip/set_partition) ----
struct tlx_addr {
    unsigned chip;
    unsigned sub_partition;
    tlx_addr() : chip(0), sub_partition(0) {}
};

// =============================================================================
// warp_inst_t MINIMAL STUB
// Only the methods actually called by gpu-cache.h/cc are stubbed.
// =============================================================================
class warp_inst_t {
public:
    warp_inst_t() : pc(0), m_is_ldgsts(false), m_is_store(false), m_is_load(false),
                    m_is_write(false) {}
    unsigned pc;
    bool m_is_ldgsts;
    bool m_is_store;
    bool m_is_load;
    bool m_is_write;
    unsigned long long get_addr(unsigned) const { return 0; }
    unsigned get_uid() const { return 0; }  // unique ID stub

    bool is_store() const { return m_is_store; }
    bool is_load() const { return m_is_load; }
    bool isatomic() const { return false; }
};

// ---- MEM_FETCH: full stub (gpu-cache.h/cc heavily depend on this) ----
class mem_fetch {
public:
    mem_fetch(const mem_access_t &access, const warp_inst_t *inst,
              unsigned long long streamID, unsigned ctrl_size,
              unsigned wid, unsigned sid, unsigned tpc,
              const shader_core_config *mem_config,
              unsigned long long cycle, class mem_fetch *orig = NULL,
              class mem_fetch *orig_wr = NULL)
        : m_access(access),
          m_inst(inst ? *inst : dummy_inst()),  // GPGPU-Sim passes NULL sometimes
          m_streamID(streamID),
          m_ctrl_size(ctrl_size), m_wid(wid), m_sid(sid), m_tpc(tpc),
          m_mem_config(mem_config), m_status(NUM_MEM_FETCH_STATUS),
          m_is_write(access.is_write()), original_mf(orig),
          original_wr_mf(orig_wr), m_data_size(access.get_size())
    {
        m_tlx_addr.chip = 0;
        m_tlx_addr.sub_partition = 0;
        m_tlx_addr.chip = 0;
        m_tlx_addr.sub_partition = 0;
    }

    // Accessors — called by cache code
    new_addr_type get_addr() const { return m_access.get_addr(); }
    unsigned get_data_size() const { return m_data_size; }
    void set_data_size(unsigned size) { m_data_size = size; }
    unsigned get_ctrl_size() const { return m_ctrl_size; }
    unsigned size() const { return m_data_size; }  // alias for get_data_size
    bool is_write() const { return m_access.is_write(); }
    bool get_is_write() const { return m_access.is_write(); }
    enum mem_access_type get_access_type() const { return m_access.get_type(); }
    unsigned long long get_streamID() const { return m_streamID; }
    unsigned get_wid() const { return m_wid; }
    unsigned get_sid() const { return m_sid; }
    unsigned get_tpc() const { return m_tpc; }
    const shader_core_config *get_mem_config() const { return m_mem_config; }
    const warp_inst_t &get_inst() { return m_inst; }
    enum mem_fetch_status get_status() const { return m_status; }
    const active_mask_t &get_access_warp_mask() const {
        return m_access.get_warp_mask();
    }
    mem_access_byte_mask_t get_access_byte_mask() const {
        return m_access.get_byte_mask();
    }
    mem_access_sector_mask_t get_access_sector_mask() const {
        return m_access.get_sector_mask();
    }
    const tlx_addr &get_tlx_addr() const { return m_tlx_addr; }
    void set_chip(unsigned c) { m_tlx_addr.chip = c; }
    void set_partition(unsigned p) { m_tlx_addr.sub_partition = p; }
    void set_addr(new_addr_type a) { m_access.m_addr = a; }  // modify address
    bool istexture() const { return m_access.get_type() == TEXTURE_ACC_R; }
    bool isatomic() const { return false; }

    void set_status(enum mem_fetch_status s, unsigned long long cycle) {
        m_status = s;
    }
    void set_reply() {}
    class mem_fetch *get_original_mf() { return original_mf; }
    class mem_fetch *get_original_wr_mf() { return original_wr_mf; }

    // Print stub
    void print(FILE *fp = stdout, bool detail = false) const {
        fprintf(fp, "mem_fetch addr=0x%llx type=%d size=%u\n",
                (unsigned long long)m_access.get_addr(),
                (int)m_access.get_type(), m_data_size);
    }

private:
    static warp_inst_t &dummy_inst() { static warp_inst_t d; return d; }
    mem_access_t m_access;
    warp_inst_t m_inst;
    unsigned long long m_streamID;
    unsigned m_ctrl_size;
    unsigned m_wid;
    unsigned m_sid;
    unsigned m_tpc;
    const shader_core_config *m_mem_config;
    enum mem_fetch_status m_status;
    bool m_is_write;
    class mem_fetch *original_mf;
    class mem_fetch *original_wr_mf;
    unsigned m_data_size;
    tlx_addr m_tlx_addr;
};

// ---- Concrete mem_fetch_allocator for testing ----
class simple_mf_allocator : public mem_fetch_allocator {
public:
    std::vector<mem_fetch *> allocated;

    mem_fetch *alloc(new_addr_type addr, mem_access_type type,
                     unsigned size, bool wr,
                     unsigned long long cycle,
                     unsigned long long streamID) const override {
        mem_access_sector_mask_t sm;
        sm.set(0);  // at least one sector must be valid
        mem_access_t access(type, addr, size, wr,
                            active_mask_t(), mem_access_byte_mask_t(), sm);
        warp_inst_t *inst = new warp_inst_t();
        if (wr) { inst->m_is_store = true; inst->m_is_write = true; }
        else { inst->m_is_load = true; }
        mem_fetch *mf = new mem_fetch(access, inst, streamID, 0, 0, 0, 0,
                                       NULL, cycle);
        const_cast<simple_mf_allocator*>(this)->allocated.push_back(mf);
        return mf;
    }

    mem_fetch *alloc(new_addr_type addr, mem_access_type type,
                     const active_mask_t &active_mask,
                     const mem_access_byte_mask_t &byte_mask,
                     const mem_access_sector_mask_t &sector_mask,
                     unsigned size, bool wr,
                     unsigned long long cycle,
                     unsigned wid, unsigned sid, unsigned tpc,
                     class mem_fetch *original_mf,
                     unsigned long long streamID) const override {
        mem_access_sector_mask_t sm = sector_mask;
        if (sm.count() == 0) sm.set(0);  // ensure at least one sector valid
        mem_access_t access(type, addr, size, wr, active_mask,
                            byte_mask, sm);
        warp_inst_t *inst = new warp_inst_t();
        mem_fetch *mf = new mem_fetch(access, inst, streamID, 0, wid, sid, tpc,
                                       NULL, cycle, original_mf);
        const_cast<simple_mf_allocator*>(this)->allocated.push_back(mf);
        return mf;
    }
};

// ---- Concrete mem_fetch_interface for testing ----
class simple_mem_interface : public mem_fetch_interface {
public:
    std::list<mem_fetch *> queue;
    unsigned max_queue_size;
    unsigned max_queue_occupancy;

    simple_mem_interface(unsigned max_size = 256)
        : max_queue_size(max_size), max_queue_occupancy(0) {}

    bool full(unsigned, bool) const override {
        return queue.size() >= max_queue_size;
    }
    void push(mem_fetch *mf) override {
        queue.push_back(mf);
        if (queue.size() > max_queue_occupancy)
            max_queue_occupancy = queue.size();
    }
};

// ---- Hashing functions (from hashing.h, used by cache_config) ----
// Inline to avoid dependency on hashing.cc
inline unsigned bitwise_hash_function(new_addr_type higher_bits, unsigned index,
                                       unsigned bank_set_num) {
    return (index ^ higher_bits) & (bank_set_num - 1);
}
inline unsigned ipoly_hash_function(new_addr_type higher_bits, unsigned index,
                                     unsigned bank_set_num) {
    unsigned long long a = (unsigned long long)higher_bits;
    unsigned h = (unsigned)a ^ (unsigned)(a >> 32);
    h = (h ^ (h >> 16)) * 0x85ebca6b;
    h = (h ^ (h >> 13)) * 0xc2b2ae35;
    h = h ^ (h >> 16);
    return h & (bank_set_num - 1);
}

// ---- linear_to_raw_address_translation stub (from addrdec.h, used by l2_cache_config) ----
class linear_to_raw_address_translation {
public:
    linear_to_raw_address_translation() {}
    unsigned set_index(new_addr_type addr, unsigned nset_log2,
                       unsigned lsize_log2) const {
        return static_cast<unsigned>((addr >> lsize_log2) & ((1u << nset_log2) - 1));
    }
    new_addr_type partition_address(new_addr_type addr) const {
        return addr;  // identity — stub
    }
};

// ---- Stub functions declared in GPGPU-Sim headers ----
inline void shader_cache_access_log(int, int, int) {
    // Stub: no-op in standalone
}

inline const char *mem_access_type_str(enum mem_access_type type) {
    switch (type) {
        case GLOBAL_ACC_R:  return "GLOBAL_ACC_R";
        case LOCAL_ACC_R:   return "LOCAL_ACC_R";
        case CONST_ACC_R:   return "CONST_ACC_R";
        case TEXTURE_ACC_R: return "TEXTURE_ACC_R";
        case GLOBAL_ACC_W:  return "GLOBAL_ACC_W";
        case LOCAL_ACC_W:   return "LOCAL_ACC_W";
        case L1_WRBK_ACC:   return "L1_WRBK_ACC";
        case L2_WRBK_ACC:   return "L2_WRBK_ACC";
        case INST_ACC_R:    return "INST_ACC_R";
        case L1_WR_ALLOC_R: return "L1_WR_ALLOC_R";
        case L2_WR_ALLOC_R: return "L2_WR_ALLOC_R";
        default:            return "UNKNOWN";
    }
}

// ---- Memory config for testing ----
class test_memory_config : public memory_config {
public:
    test_memory_config() {
        rop_latency = 160;
    }
};

// ---- Stub for stat-tool.h types used in cache_stats ----
// (cache_stats doesn't deeply depend on stat-tool; we use simple types)

// ---- Stub for gpu-misc functions ----
inline unsigned LOGB2_FUNC(unsigned x) {
    unsigned log = 0;
    while (x >>= 1) log++;
    return log;
}

#endif // GPGPU_STUBS_H
