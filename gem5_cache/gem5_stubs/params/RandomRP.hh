#ifndef __PARAMS_RANDOMRP_HH__
#define __PARAMS_RANDOMRP_HH__
#include "params/BaseReplacementPolicy.hh"
namespace gem5 {
struct RandomRPParams : public BaseReplacementPolicyParams {
    RandomRPParams() = default;
};
}
#endif
