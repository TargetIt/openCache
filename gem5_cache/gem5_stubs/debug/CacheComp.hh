#ifndef __DEBUG_CacheComp_HH__
#define __DEBUG_CacheComp_HH__
namespace gem5 { namespace debug {
struct DebugFlag_CacheComp { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_CacheComp CacheComp;
} }
#endif
