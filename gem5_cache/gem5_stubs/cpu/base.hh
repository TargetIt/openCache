#ifndef __CPU_BASE_HH__
#define __CPU_BASE_HH__
#include "params/BaseCPU.hh"
#include "sim/clocked_object.hh"
namespace gem5 {
struct BaseCPUParams : public ClockedObjectParams {};
class BaseCPU : public ClockedObject {
  public:
    BaseCPU(const BaseCPUParams &p) : ClockedObject(p) {}
};
}
#endif
