#ifndef __PARAMS_SKEWEDASSOCIATIVE_HH__
#define __PARAMS_SKEWEDASSOCIATIVE_HH__
#include "params/BaseIndexingPolicy.hh"
namespace gem5 {
struct SkewedAssociativeParams : public BaseIndexingPolicyParams {
    unsigned size = 65536;
    unsigned entry_size = 64;
};
}
#endif
