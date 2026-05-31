#ifndef __PARAMS_TREEPLRURP_HH__
#define __PARAMS_TREEPLRURP_HH__
#include "params/BaseReplacementPolicy.hh"
namespace gem5 { struct TreePLRURPParams : public BaseReplacementPolicyParams { unsigned num_leaves = 16; }; }
#endif
