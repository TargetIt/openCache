#ifndef __BASE_CALLBACK_HH__
#define __BASE_CALLBACK_HH__
namespace gem5 {
class Callback {
  public:
    virtual ~Callback() = default;
    virtual void process() = 0;
};
}
#endif
