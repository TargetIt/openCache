#ifndef __PARAMS_TAGGEDSETASSOCIATIVE_HH__
#define __PARAMS_TAGGEDSETASSOCIATIVE_HH__
#include "params/TaggedIndexingPolicy.hh"
namespace gem5 {
struct TaggedSetAssociativeParams : public TaggedIndexingPolicyParams {
    unsigned size = 65536;
    unsigned entry_size = 64;
};
}
#endif
