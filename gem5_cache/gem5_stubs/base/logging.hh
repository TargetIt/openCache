// =============================================================================
// gem5 stubs: base/logging.hh
// =============================================================================

#ifndef __BASE_LOGGING_HH__
#define __BASE_LOGGING_HH__

#include <cstdio>
#include <cstdlib>
#include <string>

#define fatal(...) do { fprintf(stderr, "FATAL: " __VA_ARGS__); fprintf(stderr, "\n"); abort(); } while(0)
#define fatal_if(cond, ...) do { if (cond) { fatal(__VA_ARGS__); } } while(0)
#define panic(...) fatal(__VA_ARGS__)
#define panic_if(cond, ...) fatal_if(cond, __VA_ARGS__)
#define warn(...) fprintf(stderr, "WARN: " __VA_ARGS__)
#define warn_if(cond, ...) do { if (cond) { warn(__VA_ARGS__); } } while(0)
#define inform(...) fprintf(stdout, "INFO: " __VA_ARGS__)
#define hack(...) fprintf(stdout, "HACK: " __VA_ARGS__)
#define warn_once(...) warn(__VA_ARGS__)
#define exit_message(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); exit(1); } while(0)
#define gem5_assert(...) ((void)0)

#endif // __BASE_LOGGING_HH__
