#ifndef __PARAMS_WEIGHTEDLRURP_HH__
#define __PARAMS_WEIGHTEDLRURP_HH__
#include "params/BaseReplacementPolicy.hh"
namespace gem5 {
struct WeightedLRURPParams : public BaseReplacementPolicyParams {
    WeightedLRURPParams() = default;
};
}
#endif
