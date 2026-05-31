#ifndef __PARAMS_WAYPOLICYALLOCATION_HH__
#define __PARAMS_WAYPOLICYALLOCATION_HH__
#include "params/BasePartitioningPolicy.hh"
namespace gem5 {
struct WayPolicyAllocationParams : public BasePartitioningPolicyParams {
    WayPolicyAllocationParams() = default;
};
}
#endif
