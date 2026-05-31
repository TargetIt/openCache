#ifndef __DEBUG_DEBUGFLAG_HH__
#define __DEBUG_DEBUGFLAG_HH__
namespace gem5 { namespace debug {
class SimpleCacheRepl { public: static bool is_on() { return false; } };
} }
#define DebugCacheRepl(name) extern gem5::debug::SimpleCacheRepl name
DebugCacheRepl(CacheRepl);
#endif
