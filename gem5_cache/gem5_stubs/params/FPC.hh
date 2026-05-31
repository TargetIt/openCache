#ifndef __PARAMS_FPC_HH__
#define __PARAMS_FPC_HH__
#include "params/BaseCacheCompressor.hh"
namespace gem5 {
struct FPCParams : public BaseCacheCompressorParams {
    FPCParams() = default;
};
}
#endif
