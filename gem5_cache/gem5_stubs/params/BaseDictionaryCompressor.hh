#ifndef __PARAMS_BASEDICTIONARYCOMPRESSOR_HH__
#define __PARAMS_BASEDICTIONARYCOMPRESSOR_HH__
#include "params/BaseCacheCompressor.hh"
namespace gem5 { struct BaseDictionaryCompressorParams : public BaseCacheCompressorParams { unsigned dictionary_size = 256; }; }
#endif
