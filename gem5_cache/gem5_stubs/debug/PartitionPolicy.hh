#ifndef __DEBUG_PartitionPolicy_HH__
#define __DEBUG_PartitionPolicy_HH__
namespace gem5 { namespace debug { struct SimpleCache { bool flag; operator bool() const { return flag; } }; } }
#define DebugPartitionPolicy(n) extern gem5::debug::SimpleCache n
DebugPartitionPolicy(Debug_PartitionPolicy);
#endif
