#ifndef __PARAMS_MULTICOMPRESSOR_HH__
#define __PARAMS_MULTICOMPRESSOR_HH__
#include "params/BaseCacheCompressor.hh"
#include <vector>
namespace gem5 {
namespace compression { class Base; }
struct MultiCompressorParams : public BaseCacheCompressorParams {
    std::vector<compression::Base*> compressors;
    bool encoding_in_tags = false;
};
}
#endif
