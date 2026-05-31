#ifndef __MEM_CACHE_PREFETCH_BASE_HH__
#define __MEM_CACHE_PREFETCH_BASE_HH__

#include <string>
#include "base/types.hh"
#include "params/BasePrefetcher.hh"
#include "sim/sim_object.hh"
#include "sim/probe/probe.hh"

namespace gem5 {

class System;
class BaseCache;

namespace prefetch {

class Base : public SimObject
{
  public:
    typedef BasePrefetcherParams Params;
    Base(const Params &p) : SimObject(p) {}
    virtual ~Base() = default;
    void setParentInfo(System *sys, ProbeManager *pm, unsigned blk_size) {}
    void incrDemandMhsrMisses() {}
    Tick nextPrefetchReadyTime() const { return MaxTick; }
    PacketPtr getPacket() { return nullptr; }
    void pfHitInCache() {}
    void pfHitInMSHR() {}
    void pfHitInWB() {}
    void prefetchUnused() {}
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_BASE_HH__
