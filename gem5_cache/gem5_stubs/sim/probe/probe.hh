#ifndef __SIM_PROBE_PROBE_HH__
#define __SIM_PROBE_PROBE_HH__
#include <cstdint>
namespace gem5 {

class ProbeManager {};

template <typename T>
class ProbePointArg {
  public:
    ProbePointArg() = default;
    ProbePointArg(ProbeManager *pm, const std::string &name) {}
    void notify(const T &t) const {}
    bool hasListeners() const { return false; }
};

}
#endif
