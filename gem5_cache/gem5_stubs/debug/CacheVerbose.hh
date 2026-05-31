#ifndef __DEBUG_CacheVerbose_HH__
#define __DEBUG_CacheVerbose_HH__
namespace gem5 { namespace debug {
struct DebugFlag_CacheVerbose { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_CacheVerbose CacheVerbose;
} }
#endif
