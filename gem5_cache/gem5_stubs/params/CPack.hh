#ifndef __PARAMS_CPACK_HH__
#define __PARAMS_CPACK_HH__
#include "params/BaseCacheCompressor.hh"
namespace gem5 {
struct CPackParams : public BaseCacheCompressorParams {
    CPackParams() = default;
};
}
#endif
