#ifndef __BASE_SAT_COUNTER_HH__
#define __BASE_SAT_COUNTER_HH__
#include <cstdint>
#include <memory>
namespace gem5 {

template <int BITS> class SatCounter {
    uint8_t val = 0;
  public:
    SatCounter() {}
    SatCounter(uint8_t v) : val(v) {}
    SatCounter& operator++() { if (val < ((1<<BITS)-1)) val++; return *this; }
    SatCounter& operator++(int) { return ++(*this); }
    SatCounter& operator--() { if (val > 0) val--; return *this; }
    SatCounter& operator--(int) { return --(*this); }
    void reset() { val = 0; }
    void reset(uint8_t v) { val = v; }
    SatCounter& operator>>=(int shift) { val >>= shift; return *this; }
    SatCounter& operator=(uint8_t v) { val = v; return *this; }
    operator uint8_t() const { return val; }
    uint8_t saturation() const { return val; }
};

typedef SatCounter<3> SatCounter8;
typedef SatCounter<4> SatCounter16;
typedef SatCounter<5> SatCounter32;

// Backward compatibility aliases
template <int BITS> using SatCounter8_t = SatCounter<BITS>;

// Saturating counter standalone
template <int BITS> class sat_counter {
    uint32_t val = 0;
  public:
    sat_counter() {}
    sat_counter(uint32_t v) : val(v) {}
    sat_counter& operator++() { if (val < ((1U<<BITS)-1)) val++; return *this; }
    sat_counter& operator--() { if (val > 0) val--; return *this; }
    void reset() { val = (1U << BITS) >> 1; }
    void reset(uint32_t v) { val = v; }
    sat_counter& operator>>=(int shift) { val >>= shift; return *this; }
    sat_counter& operator=(uint32_t v) { val = v; return *this; }
    operator uint32_t() const { return val; }
    uint32_t calcSaturation() const { return val; }
};

// Functions for sat_counter
template <int BITS>
uint32_t saturate(sat_counter<BITS> &c) { return c.calcSaturation(); }

}
#endif
