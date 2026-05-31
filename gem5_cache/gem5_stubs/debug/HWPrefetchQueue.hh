#ifndef __DEBUG_DEBUGFLAG_HH__
#define __DEBUG_DEBUGFLAG_HH__
namespace gem5 { namespace debug {
class SimpleHWPrefetchQueue { public: static bool is_on() { return false; } };
} }
#define DebugHWPrefetchQueue(name) extern gem5::debug::SimpleHWPrefetchQueue name
DebugHWPrefetchQueue(HWPrefetchQueue);
#endif
