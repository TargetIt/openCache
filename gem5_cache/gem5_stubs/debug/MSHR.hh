#ifndef __DEBUG_MSHR_HH__
#define __DEBUG_MSHR_HH__
namespace gem5 { namespace debug { struct SimpleCache { bool flag; operator bool() const { return flag; } }; } }
#define DebugMSHR(n) extern gem5::debug::SimpleCache n
DebugMSHR(Debug_MSHR);
#endif
