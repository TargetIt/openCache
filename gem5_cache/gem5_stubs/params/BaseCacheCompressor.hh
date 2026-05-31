#ifndef __PARAMS_BASECACHECOMPRESSOR_HH__
#define __PARAMS_BASECACHECOMPRESSOR_HH__
#include "params/SimObject.hh"
namespace gem5 {
struct BaseCacheCompressorParams : public SimObjectParams {
    unsigned block_size = 64;
    unsigned chunk_size_bits = 32;
    unsigned size_threshold_percentage = 100;
    unsigned comp_chunks_per_cycle = 1;
    unsigned comp_extra_latency = 0;
    unsigned decomp_chunks_per_cycle = 1;
    unsigned decomp_extra_latency = 0;
};
}
#endif
