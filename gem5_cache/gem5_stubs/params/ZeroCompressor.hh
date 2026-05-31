#ifndef __PARAMS_ZEROCOMPRESSOR_HH__
#define __PARAMS_ZEROCOMPRESSOR_HH__
#include "params/BaseCacheCompressor.hh"
namespace gem5 {
struct ZeroCompressorParams : public BaseCacheCompressorParams {
    ZeroCompressorParams() = default;
};
}
#endif
