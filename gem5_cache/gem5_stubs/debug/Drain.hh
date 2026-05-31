#ifndef __DEBUG_DEBUGFLAG_HH__
#define __DEBUG_DEBUGFLAG_HH__
namespace gem5 { namespace debug {
class SimpleDrain { public: static bool is_on() { return false; } };
} }
#define DebugDrain(name) extern gem5::debug::SimpleDrain name
DebugDrain(Drain);
#endif
