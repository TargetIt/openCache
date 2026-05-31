#ifndef __DEBUG_Drain_HH__
#define __DEBUG_Drain_HH__
namespace gem5 { namespace debug {
struct DebugFlag_Drain { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_Drain Drain;
} }
#endif
