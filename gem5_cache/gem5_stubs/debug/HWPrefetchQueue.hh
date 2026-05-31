#ifndef __DEBUG_HWPrefetchQueue_HH__
#define __DEBUG_HWPrefetchQueue_HH__
namespace gem5 { namespace debug {
struct DebugFlag_HWPrefetchQueue { bool flag = false; static bool is_on() { return false; } operator bool() const { return flag; } };
extern DebugFlag_HWPrefetchQueue HWPrefetchQueue;
} }
#endif
