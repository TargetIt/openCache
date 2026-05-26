// =============================================================================
// Ported from GPGPU-Sim (gpgpu-sim_distribution)
//   gpu-cache.h:47       enum cache_block_state       → BlockState
//   gpu-cache.h:49-56    enum cache_request_status     → AccessStatus
//   gpu-cache.h:59-64    enum cache_reservation_fail   → ReservationFailReason
//   gpu-cache.h:68-70    enum cache_event_type         → CacheEventType
//   gpu-cache.h:75-77    enum cache_gpu_level          → CacheLevel (+L3)
//   gpu-cache.h:82-105   struct evicted_block_info     → EvictedBlockInfo
//   gpu-cache.h:107-123  struct cache_event            → CacheEvent
//   gpu-cache.h:514      enum replacement_policy_t     → ReplacementPolicy (+RANDOM,+PLRU)
//   gpu-cache.h:516-522  enum write_policy_t           → WritePolicy
//   gpu-cache.h:524      enum allocation_policy_t      → AllocationPolicy (-STREAMING)
//   gpu-cache.h:526-531  enum write_allocate_policy_t  → WriteAllocatePolicy
//   gpu-cache.h:533-538  enum mshr_config_t            → MSHRType
//   gpu-cache.h:540-546  enum set_index_function       → SetIndexFunction (-FERMI_HASH)
//   gpu-cache.h:548      enum cache_type               → CacheType
//   gpu-cache.cc:42-55   cache_request_status_str()    → access_status_str()
//   gpu-cache.cc:515-567 was_write_sent / was_read_sent → inline helpers
//
// NEW (no direct GPGPU-Sim equivalent):
//   AccessType  — replaces mem_access_type from abstract_hardware_model.h
//   CacheRequest — standalone value type replacing mem_fetch*
//   CacheResult — return-value wrapper
//
// Key modifications:
//   - C++11 enum class with :uint8_t for type safety
//   - namespace opencache wrapping
//   - addr_t = uint64_t instead of new_addr_type
//   - byte_mask_t / sector_mask_t use std::bitset
//   - ENRTY→ENTRY spelling fix in ReservationFailReason
//   - CacheRequest::is_global_access field added for LOCAL_WB_GLOBAL_WT
//     (Bug 4.5 fix: replaces GPGPU-Sim's GLOBAL_ACC_W enum check)
// =============================================================================

#ifndef OPEN_CACHE_TYPES_H
#define OPEN_CACHE_TYPES_H

#include <cstdint>
#include <bitset>
#include <vector>

namespace opencache {

// Address type
using addr_t = uint64_t;

// Sector configuration
constexpr unsigned DEFAULT_SECTOR_CHUNK_SIZE = 4;
constexpr unsigned DEFAULT_SECTOR_SIZE = 32;  // 32 bytes per sector
constexpr unsigned MAX_BYTE_MASK_SIZE = 128;

using byte_mask_t = std::bitset<MAX_BYTE_MASK_SIZE>;
using sector_mask_t = std::bitset<DEFAULT_SECTOR_CHUNK_SIZE>;

// Cache block states
enum class BlockState : uint8_t {
    INVALID = 0,
    RESERVED,   // allocated but not yet filled
    VALID,
    MODIFIED
};

// Cache request status
enum class AccessStatus : uint8_t {
    HIT = 0,
    HIT_RESERVED,
    MISS,
    RESERVATION_FAIL,
    SECTOR_MISS,
    MSHR_HIT,
    NUM_STATUS
};

// Reservation failure reasons
enum class ReservationFailReason : uint8_t {
    LINE_ALLOC_FAIL = 0,
    MISS_QUEUE_FULL,
    MSHR_ENTRY_FAIL,
    MSHR_MERGE_ENTRY_FAIL,
    MSHR_RW_PENDING,
    NUM_FAIL_REASONS
};

// Cache event types
enum class CacheEventType : uint8_t {
    WRITE_BACK_REQUEST_SENT,
    READ_REQUEST_SENT,
    WRITE_REQUEST_SENT,
    WRITE_ALLOCATE_SENT
};

// Cache level
enum class CacheLevel : uint8_t {
    L1 = 0,
    L2,
    L3,
    OTHER,
    NUM_LEVELS
};

// Replacement policy
enum class ReplacementPolicy : uint8_t {
    LRU = 0,
    FIFO,
    RANDOM,
    PLRU,       // pseudo-LRU (tree-based)
    NUM_POLICIES
};

// Write policy
enum class WritePolicy : uint8_t {
    READ_ONLY = 0,
    WRITE_BACK,
    WRITE_THROUGH,
    WRITE_EVICT,
    LOCAL_WB_GLOBAL_WT
};

// Allocation policy
enum class AllocationPolicy : uint8_t {
    ON_MISS = 0,
    ON_FILL
};

// Write allocation policy
enum class WriteAllocatePolicy : uint8_t {
    NO_WRITE_ALLOCATE = 0,
    WRITE_ALLOCATE,          // send both read and write on write miss
    FETCH_ON_WRITE,          // send fetch on every write miss
    LAZY_FETCH_ON_READ       // write-allocate, fetch only on read
};

// Cache type
enum class CacheType : uint8_t {
    NORMAL = 0,
    SECTOR
};

// MSHR type
enum class MSHRType : uint8_t {
    TEX_FIFO = 0,
    ASSOC,
    SECTOR_TEX_FIFO,
    SECTOR_ASSOC
};

// Set index function
enum class SetIndexFunction : uint8_t {
    LINEAR = 0,
    BITWISE_XOR,
    HASH_IPOLY,
    CUSTOM
};

// Memory access type
enum class AccessType : uint8_t {
    READ = 0,
    WRITE,
    PREFETCH,
    WRITE_BACK,
    WRITE_ALLOCATE,
    WRITE_VALIDATE,      // write-allocate: mark dirty without fetch
    NUM_ACCESS_TYPES
};

// Access request structure
struct CacheRequest {
    addr_t address;
    AccessType type;
    uint32_t size;           // request size in bytes
    uint32_t stream_id;      // for multi-stream trace
    uint64_t instruction_id; // for trace ordering
    byte_mask_t byte_mask;   // which bytes in the line are accessed
    sector_mask_t sector_mask; // which sectors are accessed
    bool is_global_access;   // for LOCAL_WB_GLOBAL_WT: global vs local

    CacheRequest() : address(0), type(AccessType::READ), size(4),
                     stream_id(0), instruction_id(0),
                     is_global_access(true) {
        byte_mask.set();
        sector_mask.set();
    }

    CacheRequest(addr_t addr, AccessType t, uint32_t sz = 4,
                 uint32_t sid = 0, uint64_t iid = 0)
        : address(addr), type(t), size(sz), stream_id(sid),
          instruction_id(iid), is_global_access(true) {
        byte_mask.set();
        sector_mask.set();
    }

    bool is_write() const {
        return type == AccessType::WRITE ||
               type == AccessType::WRITE_BACK ||
               type == AccessType::WRITE_ALLOCATE;
    }

    bool is_read() const {
        return type == AccessType::READ ||
               type == AccessType::PREFETCH;
    }
};

// Cache access result (returned to caller)
struct CacheResult {
    AccessStatus status;
    uint64_t latency;        // cycles taken
    bool is_hit;

    CacheResult() : status(AccessStatus::MISS), latency(0), is_hit(false) {}
    CacheResult(AccessStatus s, uint64_t lat)
        : status(s), latency(lat), is_hit(s == AccessStatus::HIT) {}
};

// Evicted block info
struct EvictedBlockInfo {
    addr_t block_addr;
    uint32_t modified_size;
    byte_mask_t byte_mask;
    sector_mask_t sector_mask;

    EvictedBlockInfo() : block_addr(0), modified_size(0) {
        byte_mask.reset();
        sector_mask.reset();
    }
};

// Cache event for lower-level communication
struct CacheEvent {
    CacheEventType type;
    EvictedBlockInfo evicted_block;

    explicit CacheEvent(CacheEventType t) : type(t) {}
    CacheEvent(CacheEventType t, const EvictedBlockInfo &evicted)
        : type(t), evicted_block(evicted) {}
};

// Helper to check event types
inline bool was_write_sent(const std::vector<CacheEvent> &events) {
    for (const auto &e : events) {
        if (e.type == CacheEventType::WRITE_BACK_REQUEST_SENT ||
            e.type == CacheEventType::WRITE_REQUEST_SENT)
            return true;
    }
    return false;
}

inline bool was_read_sent(const std::vector<CacheEvent> &events) {
    for (const auto &e : events) {
        if (e.type == CacheEventType::READ_REQUEST_SENT) return true;
    }
    return false;
}

inline bool was_writeallocate_sent(const std::vector<CacheEvent> &events) {
    for (const auto &e : events) {
        if (e.type == CacheEventType::WRITE_ALLOCATE_SENT) return true;
    }
    return false;
}

// Access request status strings
inline const char *access_status_str(AccessStatus status) {
    switch (status) {
        case AccessStatus::HIT:              return "HIT";
        case AccessStatus::HIT_RESERVED:     return "HIT_RESERVED";
        case AccessStatus::MISS:             return "MISS";
        case AccessStatus::RESERVATION_FAIL: return "RESERVATION_FAIL";
        case AccessStatus::SECTOR_MISS:      return "SECTOR_MISS";
        case AccessStatus::MSHR_HIT:         return "MSHR_HIT";
        default:                             return "UNKNOWN";
    }
}

} // namespace opencache

#endif // OPEN_CACHE_TYPES_H
