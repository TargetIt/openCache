#ifndef __PARAMS_DELTACORRELATINGPREDICTIONTABLES_HH__
#define __PARAMS_DELTACORRELATINGPREDICTIONTABLES_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct DeltaCorrelatingPredictionTablesParams : public BasePrefetcherParams {
    DeltaCorrelatingPredictionTablesParams() = default;
};
}
#endif
