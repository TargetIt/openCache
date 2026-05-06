#include "cache_config.h"
#include <cstring>
#include <cstdio>

namespace opencache {

uint32_t CacheConfig::hash_addr(addr_t addr, uint32_t nset, uint32_t lsize_log2,
                                 uint32_t nset_log2, SetIndexFunction func) const {
    uint32_t index = static_cast<uint32_t>((addr >> lsize_log2) & (nset - 1));

    switch (func) {
        case SetIndexFunction::LINEAR:
            return index;

        case SetIndexFunction::BITWISE_XOR: {
            // Simple bitwise XOR for better distribution
            uint32_t high_bits = static_cast<uint32_t>(
                (addr >> (lsize_log2 + nset_log2)) & (nset - 1));
            return index ^ high_bits;
        }

        case SetIndexFunction::HASH_IPOLY: {
            // Irreducible polynomial hash (simplified)
            uint64_t a = addr >> lsize_log2;
            uint32_t h = static_cast<uint32_t>(a);
            h ^= static_cast<uint32_t>(a >> 32);
            h = (h ^ (h >> 16)) * 0x85ebca6b;
            h = (h ^ (h >> 13)) * 0xc2b2ae35;
            h = h ^ (h >> 16);
            return h & (nset - 1);
        }

        case SetIndexFunction::CUSTOM:
            // Fall through to linear for default
        default:
            return index;
    }
}

uint32_t CacheConfig::get_set_index(addr_t addr) const {
    return hash_addr(addr, num_sets, line_size_log2, num_sets_log2, set_index_func);
}

uint32_t CacheConfig::get_bank_index(addr_t addr) const {
    if (num_banks <= 1) return 0;
    // Bank interleaving at byte granularity
    uint32_t bank_bits = log2_u32(num_banks);
    return static_cast<uint32_t>((addr >> 2) & (num_banks - 1));
}

std::string CacheConfig::to_config_string() const {
    char buf[256];
    snprintf(buf, sizeof(buf), "%c:%u:%u:%u,%c:%c:%c:%c:%c,%c:%u:%u,%u",
             cache_type == CacheType::SECTOR ? 'S' : 'N',
             num_sets, line_size, associativity,
             replacement_policy == ReplacementPolicy::LRU ? 'L' : 'F',
             write_policy == WritePolicy::READ_ONLY ? 'R' :
             write_policy == WritePolicy::WRITE_BACK ? 'B' :
             write_policy == WritePolicy::WRITE_THROUGH ? 'T' :
             write_policy == WritePolicy::WRITE_EVICT ? 'E' : 'L',
             alloc_policy == AllocationPolicy::ON_MISS ? 'm' : 'f',
             write_alloc_policy == WriteAllocatePolicy::NO_WRITE_ALLOCATE ? 'N' :
             write_alloc_policy == WriteAllocatePolicy::WRITE_ALLOCATE ? 'W' :
             write_alloc_policy == WriteAllocatePolicy::FETCH_ON_WRITE ? 'F' : 'L',
             set_index_func == SetIndexFunction::LINEAR ? 'L' :
             set_index_func == SetIndexFunction::BITWISE_XOR ? 'X' :
             set_index_func == SetIndexFunction::HASH_IPOLY ? 'P' : 'C',
             mshr_type == MSHRType::ASSOC ? 'A' : 'S',
             mshr_entries, mshr_max_merge, miss_queue_size);
    return std::string(buf);
}

bool CacheConfig::parse_config_string(const char *config_str) {
    if (!config_str || !config_str[0]) return false;

    // Handle "none" for disabled cache
    if (strcmp(config_str, "none") == 0) {
        return false;
    }

    char ct, rp, wp, ap, wap, sif, mshr_type_ch;
    int ntok = sscanf(config_str,
        "%c:%u:%u:%u,%c:%c:%c:%c:%c,%c:%u:%u,%u",
        &ct, &num_sets, &line_size, &associativity,
        &rp, &wp, &ap, &wap, &sif,
        &mshr_type_ch, &mshr_entries, &mshr_max_merge, &miss_queue_size);

    if (ntok < 12) {
        // Try simplified format: nsets:bsize:assoc
        ntok = sscanf(config_str, "%u:%u:%u",
                      &num_sets, &line_size, &associativity);
        if (ntok >= 3) {
            compute_derived();
            return true;
        }
        exit_parse_error(config_str);
        return false;
    }

    // Parse cache type
    switch (ct) {
        case 'N': cache_type = CacheType::NORMAL; break;
        case 'S': cache_type = CacheType::SECTOR; break;
        default: exit_parse_error(config_str); return false;
    }

    // Parse replacement policy
    switch (rp) {
        case 'L': replacement_policy = ReplacementPolicy::LRU; break;
        case 'F': replacement_policy = ReplacementPolicy::FIFO; break;
        default: exit_parse_error(config_str); return false;
    }

    // Parse write policy
    switch (wp) {
        case 'R': write_policy = WritePolicy::READ_ONLY; break;
        case 'B': write_policy = WritePolicy::WRITE_BACK; break;
        case 'T': write_policy = WritePolicy::WRITE_THROUGH; break;
        case 'E': write_policy = WritePolicy::WRITE_EVICT; break;
        case 'L': write_policy = WritePolicy::LOCAL_WB_GLOBAL_WT; break;
        default: exit_parse_error(config_str); return false;
    }

    // Parse allocation policy
    switch (ap) {
        case 'm': alloc_policy = AllocationPolicy::ON_MISS; break;
        case 'f': alloc_policy = AllocationPolicy::ON_FILL; break;
        case 's':
            alloc_policy = AllocationPolicy::ON_FILL; // streaming = on_fill
            break;
        default: exit_parse_error(config_str); return false;
    }

    // Parse write allocation policy
    switch (wap) {
        case 'N': write_alloc_policy = WriteAllocatePolicy::NO_WRITE_ALLOCATE; break;
        case 'W': write_alloc_policy = WriteAllocatePolicy::WRITE_ALLOCATE; break;
        case 'F': write_alloc_policy = WriteAllocatePolicy::FETCH_ON_WRITE; break;
        case 'L': write_alloc_policy = WriteAllocatePolicy::LAZY_FETCH_ON_READ; break;
        default: exit_parse_error(config_str); return false;
    }

    // Parse MSHR type
    switch (mshr_type_ch) {
        case 'A': mshr_type = MSHRType::ASSOC; break;
        case 'S': mshr_type = MSHRType::SECTOR_ASSOC; break;
        case 'F': mshr_type = MSHRType::TEX_FIFO; break;
        case 'T': mshr_type = MSHRType::SECTOR_TEX_FIFO; break;
        default: exit_parse_error(config_str); return false;
    }

    // Parse set index function
    switch (sif) {
        case 'L': set_index_func = SetIndexFunction::LINEAR; break;
        case 'X': set_index_func = SetIndexFunction::BITWISE_XOR; break;
        case 'H':
        case 'P': set_index_func = SetIndexFunction::HASH_IPOLY; break;
        case 'C': set_index_func = SetIndexFunction::CUSTOM; break;
        default: exit_parse_error(config_str); return false;
    }

    compute_derived();
    return true;
}

} // namespace opencache
