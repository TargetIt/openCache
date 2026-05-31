#ifndef __SIM_SERIALIZE_HH__
#define __SIM_SERIALIZE_HH__
#include <string>
#include <iostream>
namespace gem5 {
class CheckpointOut {};
class CheckpointIn {};
class Serializable {
  public:
    virtual ~Serializable() = default;
    virtual void serialize(CheckpointOut &cp) const {}
    virtual void unserialize(CheckpointIn &cp) {}
};
}
#define SERIALIZE_SCALAR(obj, ...) ((void)0)
#define UNSERIALIZE_SCALAR(obj, ...) ((void)0)
#endif
