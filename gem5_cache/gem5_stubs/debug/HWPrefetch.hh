#ifndef __DEBUG_HWPrefetch_HH__
#define __DEBUG_HWPrefetch_HH__
namespace gem5 { namespace debug {
struct DebugFlag_HWPrefetch { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_HWPrefetch HWPrefetch;
} }
#endif
