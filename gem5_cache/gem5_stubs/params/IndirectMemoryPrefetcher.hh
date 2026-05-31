#ifndef __PARAMS_INDIRECTMEMORYPREFETCHER_HH__
#define __PARAMS_INDIRECTMEMORYPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct IndirectMemoryPrefetcherParams : public BasePrefetcherParams {
    IndirectMemoryPrefetcherParams() = default;
};
}
#endif
