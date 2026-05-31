#ifndef __PARAMS_DUELINGRP_HH__
#define __PARAMS_DUELINGRP_HH__
#include "params/BaseReplacementPolicy.hh"
#include <memory>
namespace gem5 {
namespace replacement_policy { class Base; }
struct DuelingRPParams : public BaseReplacementPolicyParams {
    replacement_policy::Base *replacement_policy_a = nullptr;
    replacement_policy::Base *replacement_policy_b = nullptr;
    unsigned constituency_size = 64;
    unsigned team_size = 4;
};
}
#endif
