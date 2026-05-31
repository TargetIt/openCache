#ifndef __PARAMS_QUEUEDPREFETCHER_HH__
#define __PARAMS_QUEUEDPREFETCHER_HH__
#include "params/BasePrefetcher.hh"
namespace gem5 {
struct QueuedPrefetcherParams : public BasePrefetcherParams {
    unsigned queue_size = 32;
    unsigned max_prefetch_requests_with_pending_translation = 32;
    Cycles latency = Cycles(1);
    bool queue_squash = true;
    bool queue_filter = true;
    bool cache_snoop = true;
    bool tag_prefetch = true;
    unsigned throttle_control_percentage = 0;
};
}
#endif
