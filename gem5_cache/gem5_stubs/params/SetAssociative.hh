#ifndef __PARAMS_SETASSOCIATIVE_HH__
#define __PARAMS_SETASSOCIATIVE_HH__
#include "params/BaseIndexingPolicy.hh"
namespace gem5 {
struct SetAssociativeParams : public BaseIndexingPolicyParams {
    unsigned size = 65536;
    unsigned entry_size = 64;
};
}
#endif
