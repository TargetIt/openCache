#ifndef __DEBUG_EVENT_HH__
#define __DEBUG_EVENT_HH__
namespace gem5 { namespace debug { class SimpleFlag { public: static bool is_on() { return false; } }; } }
#define DebugFlag(name) extern gem5::debug::SimpleFlag name
DebugFlag(Event);
#endif
