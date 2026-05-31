#ifndef __SIM_CUR_TICK_HH__
#define __SIM_CUR_TICK_HH__
#include "base/types.hh"
namespace gem5 {
inline Tick curTick() { static Tick _tick = 0; return _tick; }
}
#endif
