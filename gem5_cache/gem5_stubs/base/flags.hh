#ifndef __BASE_FLAGS_HH__
#define __BASE_FLAGS_HH__
#include <cstdint>
namespace gem5 {

template <typename T>
class Flags {
  private:
    T _flags = 0;
  public:
    typedef T Type;
    constexpr Flags() : _flags(0) {}
    constexpr Flags(T f) : _flags(f) {}
    const Flags<T>& operator=(T flags) { _flags = flags; return *this; }
    Flags& operator|=(T f) { _flags |= f; return *this; }
    constexpr Flags operator|(T f) const { return Flags(_flags | f); }
    bool isSet(T mask) const { return (_flags & mask); }
    bool allSet(T mask) const { return (_flags & mask) == mask; }
    bool noneSet(T mask) const { return (_flags & mask) == 0; }
    void clear() { _flags = 0; }
    void clear(T mask) { _flags &= ~mask; }
    void set(T mask) { _flags |= mask; }
    void set(T mask, bool condition) { condition ? set(mask) : clear(mask); }
    constexpr operator T() const { return _flags; }
    constexpr Flags operator~() const { return Flags(~_flags); }
    bool operator==(const Flags &o) const { return _flags == o._flags; }
    bool operator!=(const Flags &o) const { return _flags != o._flags; }
};

}
#endif
