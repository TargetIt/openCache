#ifndef __DEBUG_CacheTags_HH__
#define __DEBUG_CacheTags_HH__
namespace gem5 { namespace debug {
struct DebugFlag_CacheTags { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_CacheTags CacheTags;
} }
#endif
