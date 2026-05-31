#ifndef __PARAMS_FALRU_HH__
#define __PARAMS_FALRU_HH__
#include "params/BaseTags.hh"
namespace gem5 { struct FALRUParams : public BaseTagsParams { unsigned min_tracked_cache_size = 0; }; }
#endif
