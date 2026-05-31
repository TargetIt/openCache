#ifndef __PARAMS_FREQUENTVALUESCOMPRESSOR_HH__
#define __PARAMS_FREQUENTVALUESCOMPRESSOR_HH__
#include "params/BaseCacheCompressor.hh"
namespace gem5 {
struct FrequentValuesCompressorParams : public BaseCacheCompressorParams {
    FrequentValuesCompressorParams() = default;
};
}
#endif
