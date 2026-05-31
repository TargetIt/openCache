#ifndef __PARAMS_PARTITIONMANAGER_HH__
#define __PARAMS_PARTITIONMANAGER_HH__
#include "params/BasePartitioningPolicy.hh"
#include <vector>
namespace gem5 {
namespace partitioning_policy { class BasePartitioningPolicy; }
struct PartitionManagerParams : public BasePartitioningPolicyParams {
    std::vector<partitioning_policy::BasePartitioningPolicy*> partitioning_policies;
};
}
#endif
