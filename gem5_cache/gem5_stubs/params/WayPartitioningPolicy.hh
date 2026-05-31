#ifndef __PARAMS_WAYPARTITIONINGPOLICY_HH__
#define __PARAMS_WAYPARTITIONINGPOLICY_HH__
#include "params/BasePartitioningPolicy.hh"
namespace gem5 {
struct WayPartitioningPolicyParams : public BasePartitioningPolicyParams {
    WayPartitioningPolicyParams() = default;
};
}
#endif
