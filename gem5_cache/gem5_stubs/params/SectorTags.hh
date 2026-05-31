#ifndef __PARAMS_SECTORTAGS_HH__
#define __PARAMS_SECTORTAGS_HH__
#include "params/BaseTags.hh"
#include <memory>
namespace gem5 {
namespace replacement_policy { class Base; }
struct SectorTagsParams : public BaseTagsParams {
    int assoc = 8;
    bool sequential_access = false;
    replacement_policy::Base *replacement_policy = nullptr;
    unsigned num_blocks_per_sector = 4;
};
}
#endif
