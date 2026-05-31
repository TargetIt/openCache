#ifndef __DEBUG_DEBUGFLAG_HH__
#define __DEBUG_DEBUGFLAG_HH__
namespace gem5 { namespace debug {
class SimpleCachePort { public: static bool is_on() { return false; } };
} }
#define DebugCachePort(name) extern gem5::debug::SimpleCachePort name
DebugCachePort(CachePort);
#endif
