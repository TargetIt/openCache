#ifndef __PARAMS_SHIPRP_HH__
#define __PARAMS_SHIPRP_HH__
#include "params/BRRIPRP.hh"
namespace gem5 { struct SHiPRPParams : public BRRIPRPParams { unsigned insertion_threshold = 50; unsigned shct_size = 16384; }; }
#endif
