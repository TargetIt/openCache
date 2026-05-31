#ifndef __SIM_CLOCKED_OBJECT_HH__
#define __SIM_CLOCKED_OBJECT_HH__

#include "base/types.hh"
#include "params/ClockedObject.hh"
#include "sim/clock_domain.hh"
#include "sim/core.hh"
#include "sim/cur_tick.hh"
#include "sim/power_state.hh"
#include "sim/sim_object.hh"

namespace gem5 {

class Clocked
{
  private:
    mutable Tick tick = 0;
    mutable Cycles cycle = Cycles(0);
    ClockDomain &clockDomain;
  protected:
    Clocked(ClockDomain &cd) : tick(0), cycle(0), clockDomain(cd) {}
    virtual ~Clocked() {}
    void resetClock() const {}
    virtual void clockPeriodUpdated() {}
  public:
    inline Tick clockEdge(Cycles c = Cycles(0)) const { return 0; }
    inline Cycles curCycle() const { return Cycles(0); }
    Tick nextCycle() const { return 0; }
    uint64_t frequency() const { return 1000000; }
    Tick clockPeriod() const { return 1000; }
    double voltage() const { return 1.0; }
    Cycles ticksToCycles(Tick t) const { return Cycles(0); }
    Tick cyclesToTicks(Cycles c) const { return 0; }
};

class ClockedObject : public SimObject, public Clocked
{
  public:
    ClockedObject(const ClockedObjectParams &p);
    using Params = ClockedObjectParams;
    void serialize(CheckpointOut &cp) const override {}
    void unserialize(CheckpointIn &cp) override {}
    PowerState *powerState = nullptr;
};

} // namespace gem5
#endif
