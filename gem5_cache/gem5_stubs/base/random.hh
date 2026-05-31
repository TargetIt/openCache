#ifndef __BASE_RANDOM_HH__
#define __BASE_RANDOM_HH__
#include <cstdint>
#include <cstdlib>
#include <memory>
namespace gem5 {
class Random {
  public:
    typedef std::shared_ptr<Random> RandomPtr;
    Random() {}
    Random(uint32_t seed) {}
    template <typename T> T random(T min, T max) { return min + (T)(rand() % (int)(max - min + 1)); }
    uint64_t random() { return (uint64_t)rand() << 32 | rand(); }
    static RandomPtr genRandom() { return std::make_shared<Random>(); }
};
}
#endif
