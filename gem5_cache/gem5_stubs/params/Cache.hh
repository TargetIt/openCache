#ifndef __PARAMS_CACHE_HH__
#define __PARAMS_CACHE_HH__
#include "params/BaseCache.hh"
#include <memory>
namespace gem5 {
namespace replacement_policy { class Base; }
struct CacheParams : public BaseCacheParams {
    replacement_policy::Base *replacement_policy = nullptr;
};
}
#endif
