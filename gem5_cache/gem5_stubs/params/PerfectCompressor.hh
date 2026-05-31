#ifndef __PARAMS_PERFECTCOMPRESSOR_HH__
#define __PARAMS_PERFECTCOMPRESSOR_HH__
#include "params/BaseCacheCompressor.hh"
namespace gem5 { struct PerfectCompressorParams : public BaseCacheCompressorParams { unsigned max_compression_ratio = 2; }; }
#endif
