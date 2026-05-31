#ifndef __DEBUG_CacheRepl_HH__
#define __DEBUG_CacheRepl_HH__
namespace gem5 { namespace debug {
struct DebugFlag_CacheRepl { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_CacheRepl CacheRepl;
} }
#endif
