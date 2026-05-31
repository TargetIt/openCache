#ifndef __DEBUG_DEBUGFLAG_HH__
#define __DEBUG_DEBUGFLAG_HH__
namespace gem5 { namespace debug {
class SimpleCacheVerbose { public: static bool is_on() { return false; } };
} }
#define DebugCacheVerbose(name) extern gem5::debug::SimpleCacheVerbose name
DebugCacheVerbose(CacheVerbose);
#endif
