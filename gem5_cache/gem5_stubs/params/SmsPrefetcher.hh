#ifndef __PARAMS_SMSPREFETCHER_HH__
#define __PARAMS_SMSPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct SmsPrefetcherParams : public BasePrefetcherParams {
    SmsPrefetcherParams() = default;
};
}
#endif
