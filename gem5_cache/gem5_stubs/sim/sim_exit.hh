#ifndef __SIM_SIM_EXIT_HH__
#define __SIM_SIM_EXIT_HH__
#include <cstdint>
#include <functional>
namespace gem5 {
void exitSimLoop(const std::string &msg, int exit_code = 0, Tick when = 0);
using ExitCallback = std::function<void()>;
void registerExitCallback(const ExitCallback &callback);
}
#endif
