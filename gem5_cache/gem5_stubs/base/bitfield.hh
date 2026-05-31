#ifndef __BASE_BITFIELD_HH__
#define __BASE_BITFIELD_HH__
#include <cstdint>
template <typename T> T bits(T val, unsigned first, unsigned last) {
    return (val >> first) & ((static_cast<T>(1) << (last - first + 1)) - 1);
}
template <typename T> T bits(T val, unsigned bit) { return bits(val, bit, bit); }
template <typename T> T mbits(T val, unsigned first, unsigned last) { return bits(val, first, last); }
template <typename T1, typename T2> T1 insertBits(T1 val, unsigned first, unsigned last, T2 bit_val) {
    T1 mask = (static_cast<T1>(1) << (last - first + 1)) - 1;
    val &= ~(mask << first);
    val |= (static_cast<T1>(bit_val) & mask) << first;
    return val;
}
template <typename T1, typename T2> T1 insertBits(T1 val, unsigned bit, T2 bit_val) {
    return insertBits(val, bit, bit, bit_val);
}
inline uint64_t alignToPowerOfTwo(uint64_t val) {
    val--; val |= val >> 1; val |= val >> 2; val |= val >> 4;
    val |= val >> 8; val |= val >> 16; val |= val >> 32; return val + 1;
}

// Population count: number of set bits
template <typename T>
inline int popCount(T val) {
    int count = 0;
    while (val) { count += val & 1; val >>= 1; }
    return count;
}

// Create a mask with N low bits set
template <typename T>
inline T mask(int n) {
    return (static_cast<T>(1) << n) - 1;
}
// Non-template overload for when T cannot be deduced from the call context
inline unsigned mask(int n) {
    return (1u << n) - 1;
}
#endif
