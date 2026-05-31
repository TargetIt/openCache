#ifndef __BASE_SAT_COUNTER_HH__
#define __BASE_SAT_COUNTER_HH__

#include <cassert>
#include <cstdint>
#include <cstdlib>

namespace gem5
{

/**
 * Implements an n bit saturating counter.
 * Matches the gem5 GenericSatCounter<T> interface.
 */
template <class T>
class GenericSatCounter
{
  public:
    /** No default construction — must specify bit width. */
    GenericSatCounter() = delete;

    explicit GenericSatCounter(unsigned bits, T initial_val = 0)
        : initialVal(initial_val), maxVal((1ULL << bits) - 1),
          counter(initial_val)
    {
    }

    GenericSatCounter(const GenericSatCounter& other)
        : initialVal(other.initialVal), maxVal(other.maxVal),
          counter(other.counter) {}

    GenericSatCounter& operator=(const GenericSatCounter& other) {
        if (this != &other) {
            initialVal = other.initialVal;
            maxVal = other.maxVal;
            counter = other.counter;
        }
        return *this;
    }

    GenericSatCounter(GenericSatCounter&& other)
        : initialVal(other.initialVal), maxVal(other.maxVal),
          counter(other.counter)
    {
        other.counter = 0;
    }

    GenericSatCounter& operator=(GenericSatCounter&& other) {
        if (this != &other) {
            initialVal = other.initialVal;
            maxVal = other.maxVal;
            counter = other.counter;
            other.counter = 0;
        }
        return *this;
    }

    GenericSatCounter& operator++() {
        if (counter < maxVal) ++counter;
        return *this;
    }
    GenericSatCounter operator++(int) {
        GenericSatCounter old = *this;
        ++*this;
        return old;
    }
    GenericSatCounter& operator--() {
        if (counter > 0) --counter;
        return *this;
    }
    GenericSatCounter operator--(int) {
        GenericSatCounter old = *this;
        --*this;
        return old;
    }

    GenericSatCounter& operator>>=(const int& shift) {
        counter >>= shift;
        return *this;
    }
    GenericSatCounter& operator<<=(const int& shift) {
        counter <<= shift;
        if (counter > maxVal) counter = maxVal;
        return *this;
    }

    GenericSatCounter& operator+=(const long long& value) {
        if (value >= 0) {
            if ((T)(maxVal - counter) >= (T)value) counter += (T)value;
            else counter = maxVal;
        } else {
            *this -= -value;
        }
        return *this;
    }
    GenericSatCounter& operator-=(const long long& value) {
        if (value >= 0) {
            if (counter > (T)value) counter -= (T)value;
            else counter = 0;
        } else {
            *this += -value;
        }
        return *this;
    }

    operator T() const { return counter; }

    void reset() { counter = initialVal; }
    void reset(T v) { initialVal = v; counter = v; }

    double calcSaturation() const { return maxVal > 0 ? (double)counter / maxVal : 0.0; }

    bool isSaturated() const { return counter == maxVal; }

    T saturate() {
        T diff = maxVal - counter;
        counter = maxVal;
        return diff;
    }

  private:
    T initialVal = 0;
    T maxVal = 0;
    T counter = 0;
};

typedef GenericSatCounter<uint8_t>  SatCounter8;
typedef GenericSatCounter<uint16_t> SatCounter16;
typedef GenericSatCounter<uint32_t> SatCounter32;
typedef GenericSatCounter<uint64_t> SatCounter64;

} // namespace gem5

#endif // __BASE_SAT_COUNTER_HH__
