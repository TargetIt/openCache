#ifndef __DEBUG_Cache_HH__
#define __DEBUG_Cache_HH__
namespace gem5 { namespace debug {
struct DebugFlag_Cache { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_Cache Cache;
} }
#endif
