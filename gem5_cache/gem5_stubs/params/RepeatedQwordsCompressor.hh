#ifndef __PARAMS_REPEATEDQWORDSCOMPRESSOR_HH__
#define __PARAMS_REPEATEDQWORDSCOMPRESSOR_HH__
#include "params/BaseCacheCompressor.hh"
namespace gem5 {
struct RepeatedQwordsCompressorParams : public BaseCacheCompressorParams {
    RepeatedQwordsCompressorParams() = default;
};
}
#endif
