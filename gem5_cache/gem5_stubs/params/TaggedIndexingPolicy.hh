#ifndef __PARAMS_TAGGEDINDEXINGPOLICY_HH__
#define __PARAMS_TAGGEDINDEXINGPOLICY_HH__
#include "params/BaseIndexingPolicy.hh"
namespace gem5 {
struct TaggedIndexingPolicyParams : public BaseIndexingPolicyParams {
    unsigned size = 65536;
    unsigned entry_size = 64;
};
}
#endif
