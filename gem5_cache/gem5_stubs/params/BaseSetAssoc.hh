#ifndef __PARAMS_BASESETASSOC_HH__
#define __PARAMS_BASESETASSOC_HH__
#include "params/BaseTags.hh"
#include <memory>
namespace gem5 {
namespace replacement_policy { class Base; }
struct BaseSetAssocParams : public BaseTagsParams {
    int assoc = 8;
    bool sequential_access = false;
    replacement_policy::Base *replacement_policy = nullptr;
};
}
#endif
