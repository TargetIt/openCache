#ifndef __PARAMS_NONCOHERENTCACHE_HH__
#define __PARAMS_NONCOHERENTCACHE_HH__
#include "params/BaseCache.hh"
#include <memory>
namespace gem5 {
namespace replacement_policy { class Base; }
struct NoncoherentCacheParams : public BaseCacheParams {
    replacement_policy::Base *replacement_policy = nullptr;
};
}
#endif
