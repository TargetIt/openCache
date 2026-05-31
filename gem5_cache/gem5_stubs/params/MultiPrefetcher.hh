#ifndef __PARAMS_MULTIPREFETCHER_HH__
#define __PARAMS_MULTIPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct MultiPrefetcherParams : public BasePrefetcherParams {
    MultiPrefetcherParams() = default;
};
}
#endif
