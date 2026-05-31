#ifndef __PARAMS_SIGNATUREPATHPREFETCHER_HH__
#define __PARAMS_SIGNATUREPATHPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct SignaturePathPrefetcherParams : public BasePrefetcherParams {
    SignaturePathPrefetcherParams() = default;
};
}
#endif
