#ifndef __DEBUG_DEBUGFLAG_HH__
#define __DEBUG_DEBUGFLAG_HH__
namespace gem5 { namespace debug {
class SimpleCacheComp { public: static bool is_on() { return false; } };
} }
#define DebugCacheComp(name) extern gem5::debug::SimpleCacheComp name
DebugCacheComp(CacheComp);
#endif
