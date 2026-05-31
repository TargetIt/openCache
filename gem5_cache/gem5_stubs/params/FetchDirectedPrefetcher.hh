#ifndef __PARAMS_FETCHDIRECTEDPREFETCHER_HH__
#define __PARAMS_FETCHDIRECTEDPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct FetchDirectedPrefetcherParams : public BasePrefetcherParams {
    FetchDirectedPrefetcherParams() = default;
};
}
#endif
