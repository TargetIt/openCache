#ifndef __BASE_INTMATH_HH__
#define __BASE_INTMATH_HH__
#include <cstdint>
#include <cmath>
inline int floorLog2(unsigned x) { return 31 - __builtin_clz(x); }
inline int ceilLog2(unsigned x) { return x <= 1 ? 0 : 32 - __builtin_clz(x - 1); }
inline bool isPowerOf2(unsigned x) { return x != 0 && (x & (x - 1)) == 0; }
inline uint64_t roundUp(uint64_t val, uint64_t align) { return (val + align - 1) / align * align; }
inline uint64_t roundDown(uint64_t val, uint64_t align) { return val / align * align; }
inline int divCeil(int a, int b) { return (a + b - 1) / b; }
#endif
