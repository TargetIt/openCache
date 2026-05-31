#ifndef __PARAMS_FPCD_HH__
#define __PARAMS_FPCD_HH__
#include "params/BaseCacheCompressor.hh"
namespace gem5 {
struct FPCDParams : public BaseCacheCompressorParams {
    FPCDParams() = default;
};
}
#endif
