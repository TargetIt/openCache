#ifndef __PARAMS_MRURP_HH__
#define __PARAMS_MRURP_HH__
#include "params/BaseReplacementPolicy.hh"
namespace gem5 {
struct MRURPParams : public BaseReplacementPolicyParams {
    MRURPParams() = default;
};
}
#endif
