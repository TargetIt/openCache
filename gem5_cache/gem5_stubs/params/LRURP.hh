#ifndef __PARAMS_LRURP_HH__
#define __PARAMS_LRURP_HH__
#include "params/BaseReplacementPolicy.hh"
namespace gem5 {
struct LRURPParams : public BaseReplacementPolicyParams {
    LRURPParams() = default;
};
}
#endif
