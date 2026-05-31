#ifndef __PARAMS_BASEPREFETCHER_HH__
#define __PARAMS_BASEPREFETCHER_HH__
#include "params/ClockedObject.hh"
namespace gem5 {
class System;
struct BasePrefetcherParams : public ClockedObjectParams {
    unsigned block_size = 64;
    bool on_miss = true;
    bool on_read = true;
    bool on_write = false;
    bool on_data = true;
    bool on_inst = false;
    System *sys = nullptr;
    unsigned page_bytes = 4096;
    bool prefetch_on_access = false;
    bool prefetch_on_pf_hit = false;
    bool use_virtual_addresses = false;
};
}
#endif
