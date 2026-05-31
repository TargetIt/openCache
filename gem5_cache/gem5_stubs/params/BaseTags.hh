#ifndef __PARAMS_BASETAGS_HH__
#define __PARAMS_BASETAGS_HH__
#include "params/ClockedObject.hh"
#include "base/types.hh"
#include "mem/cache/tags/tagged_entry.hh"
#include <memory>
namespace gem5 {
class System;
namespace partitioning_policy { class PartitionManager; }
struct BaseTagsParams : public ClockedObjectParams {
    unsigned block_size = 64;
    unsigned size = 65536;
    Cycles tag_latency = Cycles(1);
    System *system = nullptr;
    TaggedIndexingPolicy *indexing_policy = nullptr;
    partitioning_policy::PartitionManager *partitioning_manager = nullptr;
    double warmup_percentage = 0.0;
};
}
#endif
