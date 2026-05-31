#ifndef __DEBUG_DEBUGFLAG_HH__
#define __DEBUG_DEBUGFLAG_HH__
namespace gem5 { namespace debug {
class SimpleHWPrefetch { public: static bool is_on() { return false; } };
} }
#define DebugHWPrefetch(name) extern gem5::debug::SimpleHWPrefetch name
DebugHWPrefetch(HWPrefetch);
#endif
