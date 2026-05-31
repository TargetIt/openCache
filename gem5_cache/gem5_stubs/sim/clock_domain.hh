#ifndef __SIM_CLOCK_DOMAIN_HH__
#define __SIM_CLOCK_DOMAIN_HH__
#include <cstdint>
#include "base/types.hh"
namespace gem5 {
class Clocked;
class ClockDomain {
 public:
  void registerWithClockDomain(Clocked *) {}
  Tick clockPeriod() const { return 1000; }
  double voltage() const { return 1.0; }
};
}
#endif
