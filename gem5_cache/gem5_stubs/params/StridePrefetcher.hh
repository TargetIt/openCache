#ifndef __PARAMS_STRIDEPREFETCHER_HH__
#define __PARAMS_STRIDEPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct StridePrefetcherParams : public BasePrefetcherParams {
    StridePrefetcherParams() = default;
};
}
#endif
