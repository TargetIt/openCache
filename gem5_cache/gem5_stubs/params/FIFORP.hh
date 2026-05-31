#ifndef __PARAMS_FIFORP_HH__
#define __PARAMS_FIFORP_HH__
#include "params/BaseReplacementPolicy.hh"
namespace gem5 {
struct FIFORPParams : public BaseReplacementPolicyParams {
    FIFORPParams() = default;
};
}
#endif
