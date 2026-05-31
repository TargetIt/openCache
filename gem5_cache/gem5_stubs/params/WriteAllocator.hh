#ifndef __PARAMS_WRITEALLOCATOR_HH__
#define __PARAMS_WRITEALLOCATOR_HH__
#include "params/SimObject.hh"
namespace gem5 {
class WriteAllocator;
struct WriteAllocatorParams : public SimObjectParams {
    unsigned coalesce_limit = 0;
    unsigned block_size = 64;
    unsigned no_allocate_limit = 0;
    Tick delay_threshold = 0;
};
}
#endif
