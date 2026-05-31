// =============================================================================
// gem5 stubs: base/compiler.hh
// =============================================================================

#ifndef __BASE_COMPILER_HH__
#define __BASE_COMPILER_HH__

#define GEM5_DEPRECATED(msg)
#define GEM5_NO_DISCARD [[nodiscard]]
#define GEM5_UNLIKELY(cond) __builtin_expect((cond), 0)

#endif // __BASE_COMPILER_HH__
