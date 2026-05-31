// =============================================================================
// gem5 stubs: base/printable.hh
// =============================================================================

#ifndef __BASE_PRINTABLE_HH__
#define __BASE_PRINTABLE_HH__

#include <ostream>
#include <string>

namespace gem5
{

class Printable
{
  public:
    virtual ~Printable() = default;
    virtual void print(std::ostream &os, int verbosity = 0,
                       const std::string &prefix = "") const = 0;
};

inline std::ostream& operator<<(std::ostream &os, const Printable &p)
{
    p.print(os);
    return os;
}

} // namespace gem5

#endif // __BASE_PRINTABLE_HH__
