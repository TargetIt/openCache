#ifndef __BASE_CONTEXT_SWITCH_TASK_ID_HH__
#define __BASE_CONTEXT_SWITCH_TASK_ID_HH__
#include <cstdint>
namespace gem5 { namespace context_switch_task_id { enum : uint32_t { Unknown = 0, MaxNormalTaskId = 1023, Prefetcher = 1024, NumTaskId = 1025 }; } }
#endif
