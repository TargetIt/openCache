#ifndef __PARAMS_BASECACHE_HH__
#define __PARAMS_BASECACHE_HH__
#include "params/ClockedObject.hh"
#include "base/types.hh"
#include "enums/Clusivity.hh"
#include <memory>
#include <vector>
namespace gem5 {
class System;
class BaseTags;
class WriteAllocator;
namespace compression { class Base; }
namespace prefetch { class Base; }
namespace partitioning_policy { class PartitionManager; }
struct BaseCacheParams : public ClockedObjectParams {
    System *system = nullptr;
    unsigned mshrs = 4;
    unsigned demand_mshr_reserve = 1;
    unsigned write_buffers = 8;
    BaseTags *tags = nullptr;
    compression::Base *compressor = nullptr;
    partitioning_policy::PartitionManager *partitioning_manager = nullptr;
    prefetch::Base *prefetcher = nullptr;
    WriteAllocator *write_allocator = nullptr;
    bool writeback_clean = false;
    Cycles tag_latency = Cycles(1);
    Cycles data_latency = Cycles(1);
    Cycles response_latency = Cycles(1);
    bool sequential_access = false;
    unsigned tgts_per_mshr = 16;
    enums::Clusivity clusivity = enums::Clusivity::mostly_incl;
    bool is_read_only = false;
    bool replace_expansions = true;
    bool move_contractions = true;
    uint64_t max_miss_count = 0;
    bool prefetch_on_access = false;
    AddrRangeList addr_ranges;
};
}
#endif
