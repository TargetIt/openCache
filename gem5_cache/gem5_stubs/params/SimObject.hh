#ifndef __PARAMS_SIMOBJECT_HH__
#define __PARAMS_SIMOBJECT_HH__
#include <string>
namespace gem5 {
struct SimObjectParams {
    std::string name;
    SimObjectParams() = default;
    virtual ~SimObjectParams() = default;
};
}
#endif
