#ifndef __PARAMS_DCPTPREFETCHER_HH__
#define __PARAMS_DCPTPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct DCPTPrefetcherParams : public BasePrefetcherParams {
    DCPTPrefetcherParams() = default;
};
}
#endif
