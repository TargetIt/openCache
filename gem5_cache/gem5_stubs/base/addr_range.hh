#ifndef __BASE_ADDR_RANGE_HH__
#define __BASE_ADDR_RANGE_HH__
#include "base/types.hh"
#include <vector>
namespace gem5 {
class AddrRange {
  public:
    Addr start() const { return 0; }
    Addr end() const { return MaxAddr; }
    Addr size() const { return MaxAddr; }
    bool valid() const { return true; }
    bool contains(Addr addr) const { return true; }
};
class AddrRangeList : public std::vector<AddrRange> {
  public:
    AddrRangeList() = default;
    template <typename Iter>
    AddrRangeList(Iter begin, Iter end) : std::vector<AddrRange>(begin, end) {}
};
}
#endif
