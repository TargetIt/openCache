#ifndef __PARAMS_MAXCAPACITYPARTITIONINGPOLICY_HH__
#define __PARAMS_MAXCAPACITYPARTITIONINGPOLICY_HH__
#include "params/BasePartitioningPolicy.hh"
namespace gem5 {
struct MaxCapacityPartitioningPolicyParams : public BasePartitioningPolicyParams {
    MaxCapacityPartitioningPolicyParams() = default;
};
}
#endif
