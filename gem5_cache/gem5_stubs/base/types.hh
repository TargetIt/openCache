// =============================================================================
// gem5 stubs: base/types.hh
// Provides fundamental types used throughout gem5 cache code.
// =============================================================================

#ifndef __BASE_TYPES_HH__
#define __BASE_TYPES_HH__

#include <inttypes.h>
#include <cassert>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <vector>

namespace gem5
{

typedef int64_t Counter;
typedef uint64_t Tick;
typedef uint64_t Addr;
typedef int16_t ThreadID;
typedef int ContextID;
typedef int16_t PortID;

const Tick MaxTick = 0xffffffffffffffffULL;
const Addr MaxAddr = (Addr)-1;
const ThreadID InvalidThreadID = (ThreadID)-1;
const ContextID InvalidContextID = (ContextID)-1;
const PortID InvalidPortID = (PortID)-1;

class Cycles
{
  private:
    uint64_t c;
  public:
    explicit constexpr Cycles(uint64_t _c) : c(_c) {}
    Cycles() : c(0) {}
    constexpr operator uint64_t() const { return c; }
    Cycles& operator++() { ++c; return *this; }
    Cycles& operator--() { assert(c != 0); --c; return *this; }
    Cycles& operator+=(const Cycles& cc) { c += cc.c; return *this; }
    constexpr bool operator>(const Cycles& cc) const { return c > cc.c; }
    constexpr Cycles operator+(const Cycles& b) const { return Cycles(c + b.c); }
    constexpr Cycles operator-(const Cycles& b) const { return Cycles(c - b.c); }
    constexpr Cycles operator<<(const int32_t shift) const { return Cycles(c << shift); }
    constexpr Cycles operator>>(const int32_t shift) const { return Cycles(c >> shift); }
};

typedef uint16_t MicroPC;
static const MicroPC MicroPCRomBit = 1 << (sizeof(MicroPC) * 8 - 1);

static inline MicroPC romMicroPC(MicroPC upc) { return upc | MicroPCRomBit; }
static inline MicroPC normalMicroPC(MicroPC upc) { return upc & ~MicroPCRomBit; }
static inline bool isRomMicroPC(MicroPC upc) { return MicroPCRomBit & upc; }

using RegVal = uint64_t;
using RegIndex = uint16_t;

static inline uint32_t floatToBits32(float val) {
    union { float f; uint32_t i; } u; u.f = val; return u.i;
}
static inline uint64_t floatToBits64(double val) {
    union { double f; uint64_t i; } u; u.f = val; return u.i;
}
static inline uint32_t floatToBits(float val) { return floatToBits32(val); }
static inline uint64_t floatToBits(double val) { return floatToBits64(val); }
static inline float bitsToFloat32(uint32_t val) {
    union { float f; uint32_t i; } u; u.i = val; return u.f;
}
static inline double bitsToFloat64(uint64_t val) {
    union { double f; uint64_t i; } u; u.i = val; return u.f;
}

class FaultBase;
typedef std::shared_ptr<FaultBase> Fault;
constexpr decltype(nullptr) NoFault = nullptr;

} // namespace gem5

#endif // __BASE_TYPES_HH__
