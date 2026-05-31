#ifndef __PARAMS_PIFPREFETCHER_HH__
#define __PARAMS_PIFPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct PIFPrefetcherParams : public BasePrefetcherParams {
    PIFPrefetcherParams() = default;
};
}
#endif
