#ifndef __PARAMS_IRREGULARSTREAMBUFFERPREFETCHER_HH__
#define __PARAMS_IRREGULARSTREAMBUFFERPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct IrregularStreamBufferPrefetcherParams : public BasePrefetcherParams {
    IrregularStreamBufferPrefetcherParams() = default;
};
}
#endif
