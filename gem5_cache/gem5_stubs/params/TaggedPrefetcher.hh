#ifndef __PARAMS_TAGGEDPREFETCHER_HH__
#define __PARAMS_TAGGEDPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct TaggedPrefetcherParams : public BasePrefetcherParams {
    TaggedPrefetcherParams() = default;
};
}
#endif
