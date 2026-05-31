#ifndef __PARAMS_BASEINDEXINGPOLICY_HH__
#define __PARAMS_BASEINDEXINGPOLICY_HH__
#include "params/SimObject.hh"
namespace gem5 { struct BaseIndexingPolicyParams : public SimObjectParams { int assoc = 4; }; }
#endif
