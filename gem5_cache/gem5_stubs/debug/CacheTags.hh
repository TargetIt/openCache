#ifndef __DEBUG_DEBUGFLAG_HH__
#define __DEBUG_DEBUGFLAG_HH__
namespace gem5 { namespace debug {
class SimpleCacheTags { public: static bool is_on() { return false; } };
} }
#define DebugCacheTags(name) extern gem5::debug::SimpleCacheTags name
DebugCacheTags(CacheTags);
#endif
