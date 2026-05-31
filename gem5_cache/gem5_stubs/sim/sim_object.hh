#ifndef __SIM_OBJECT_HH__
#define __SIM_OBJECT_HH__

#include <string>
#include <vector>
#include "base/named.hh"
#include "base/statistics.hh"
#include "base/stats/group.hh"
#include "params/SimObject.hh"
#include "sim/drain.hh"
#include "sim/eventq.hh"
#include "sim/port.hh"
#include "sim/serialize.hh"

namespace gem5 {

class ProbeManager;
class SimObject;

class SimObjectResolver {
  public:
    virtual ~SimObjectResolver() {}
    virtual SimObject *resolveSimObject(const std::string &name) = 0;
};

class SimObject : public EventManager, public Serializable, public Drainable,
                  public statistics::Group, public Named
{
  private:
    static std::vector<SimObject *> simObjectList;
    ProbeManager *probeManager = nullptr;
  protected:
    const SimObjectParams &_params;
  public:
    typedef SimObjectParams Params;
    const Params &params() const { return _params; }
    SimObject(const Params &p);
    virtual ~SimObject() = default;
    virtual void init() {}
    virtual void initState() {}
    virtual void regProbePoints() {}
    virtual void regProbeListeners() {}
    ProbeManager *getProbeManager() { return probeManager; }
    virtual Port &getPort(const std::string &if_name, PortID idx=InvalidPortID);
    virtual void startup() {}
    DrainState drain() override { return DrainState::Drained; }
    virtual void memWriteback() {}
    virtual void memInvalidate() {}
    void serialize(CheckpointOut &cp) const override {}
    void unserialize(CheckpointIn &cp) override {}
    static SimObject *find(const char *name) { return nullptr; }
};

#define PARAMS(type)                                      \
    using Params = type ## Params;                        \
    const Params &                                        \
    params() const                                        \
    {                                                     \
        return reinterpret_cast<const Params&>(_params);  \
    }

} // namespace gem5
#endif
