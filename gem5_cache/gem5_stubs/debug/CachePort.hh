#ifndef __DEBUG_CachePort_HH__
#define __DEBUG_CachePort_HH__
namespace gem5 { namespace debug {
struct DebugFlag_CachePort { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_CachePort CachePort;
} }
#endif
