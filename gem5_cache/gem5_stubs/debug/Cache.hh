#ifndef __DEBUG_DEBUGFLAG_HH__
#define __DEBUG_DEBUGFLAG_HH__
namespace gem5 { namespace debug {
class SimpleCache { public: static bool is_on() { return false; } operator bool() const { return is_on(); } };
} }
#define DebugCache(name) namespace gem5 { namespace debug { extern SimpleCache name; } }
DebugCache(Cache);
#endif
