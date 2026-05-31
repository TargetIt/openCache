#ifndef __PARAMS_BRRIPRP_HH__
#define __PARAMS_BRRIPRP_HH__
#include "params/BaseReplacementPolicy.hh"
namespace gem5 { struct BRRIPRPParams : public BaseReplacementPolicyParams { unsigned num_bits = 3; bool hit_priority = false; unsigned btp = 3; }; }
#endif
