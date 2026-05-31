#ifndef __DEBUG_MSHR_HH__
#define __DEBUG_MSHR_HH__
namespace gem5 { namespace debug {
struct DebugFlag_MSHR { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_MSHR MSHR;
} }
#endif
