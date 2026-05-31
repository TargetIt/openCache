#ifndef __SIM_SYSTEM_HH__
#define __SIM_SYSTEM_HH__
#include <cstdint>
#include <string>
#include <vector>
namespace gem5 {
class System {
  public:
    uint64_t cacheLineSize() const { return 64; }
    uint64_t getCacheLineSize() const { return 64; }
    uint32_t maxRequestors() const { return 64; }
    std::string getRequestorName(uint32_t id) const { return "requestor"; }
    bool bypassCaches() const { return false; }
};
}
#endif
