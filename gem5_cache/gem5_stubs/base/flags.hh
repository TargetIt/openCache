#ifndef __BASE_FLAGS_HH__
#define __BASE_FLAGS_HH__
#include <cstdint>
namespace gem5 {

template <typename T>
class Flags {
  private:
    T _flags = 0;
  public:
    constexpr Flags() : _flags(0) {}
    constexpr Flags(T f) : _flags(f) {}
    Flags& operator|=(T f) { _flags |= f; return *this; }
    constexpr Flags operator|(T f) const { return Flags(_flags | f); }
    bool isSet(T f) const { return (_flags & f) == f; }
    constexpr operator T() const { return _flags; }
    constexpr Flags operator~() const { return Flags(~_flags); }
    bool operator==(const Flags &o) const { return _flags == o._flags; }
    bool operator!=(const Flags &o) const { return _flags != o._flags; }
};

}
#endif
