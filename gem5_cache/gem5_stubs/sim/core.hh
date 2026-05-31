#ifndef __SIM_CORE_HH__
#define __SIM_CORE_HH__
#include "base/types.hh"
namespace gem5 {
namespace sim_clock {
    constexpr uint64_t Frequency = 1000000000000ULL;
    namespace as_int {
        constexpr uint64_t us = 1000;
        constexpr uint64_t ms = 1000000;
    }
}
Tick curTick();
}
#endif
