#ifndef __DEBUG_PARTITIONPOLICY_HH__
#define __DEBUG_PARTITIONPOLICY_HH__
namespace gem5 { namespace debug {
struct DebugFlag_PartitionPolicy { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_PartitionPolicy PartitionPolicy;
} }
#endif
