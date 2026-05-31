// =============================================================================
// gem5 stub implementations — provides definitions for symbols declared in
// stub headers but needed at link time by the cache reference code.
// =============================================================================

#include "base/types.hh"
#include "base/statistics.hh"
#include "params/SimObject.hh"
#include "params/ClockedObject.hh"
#include "params/BaseTags.hh"
#include "params/BaseIndexingPolicy.hh"
#include "sim/sim_object.hh"
#include "sim/clocked_object.hh"
#include "sim/cur_tick.hh"
#include "sim/sim_exit.hh"
#include "mem/cache/tags/partitioning_policies/partition_manager.hh"
#include "mem/cache/tags/partitioning_policies/base_pp.hh"

#include <functional>

namespace gem5 {

// === SimObject ===
SimObject::SimObject(const SimObjectParams &p) : _params(p) {
}

Port &SimObject::getPort(const std::string &if_name, PortID idx) {
    static Port dummy_port;
    return dummy_port;
}

// Dummy ClockDomain for standalone use
static ClockDomain dummyClockDomain;

// === ClockedObject ===
ClockedObject::ClockedObject(const ClockedObjectParams &p)
    : SimObject(p), Clocked(dummyClockDomain) {
}

// === registerExitCallback ===
void registerExitCallback(const std::function<void()> &cb) {
    // no-op: standalone mode doesn't need exit callbacks
}

// === Partitioning stubs ===
namespace partitioning_policy {

PartitionManager::PartitionManager(const PartitionManagerParams &p)
    : SimObject(p) {
}

void PartitionManager::notifyAcquire(uint64_t partition_id) {
    // no-op: standalone mode
}

void PartitionManager::notifyRelease(uint64_t partition_id) {
    // no-op: standalone mode
}

void PartitionManager::filterByPartition(
    std::vector<ReplaceableEntry*> &entries,
    uint64_t partition_id) const {
    // no-op: return all entries
}

BasePartitioningPolicy::BasePartitioningPolicy(const BasePartitioningPolicyParams &p)
    : SimObject(p) {
}

void BasePartitioningPolicy::filterByPartition(
    std::vector<ReplaceableEntry*> &entries,
    uint64_t partition_id) const {
    // no-op
}

} // namespace partitioning_policy

} // namespace gem5
