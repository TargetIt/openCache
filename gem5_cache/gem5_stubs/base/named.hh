#ifndef __BASE_NAMED_HH__
#define __BASE_NAMED_HH__
#include <string>
namespace gem5 {
class Named {
  public:
    Named() = default;
    Named(const std::string &n) : _name(n) {}
    virtual ~Named() = default;
    virtual std::string name() const { return _name; }
  protected:
    std::string _name;
};
}
#endif
