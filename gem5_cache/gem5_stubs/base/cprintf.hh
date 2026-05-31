#ifndef __BASE_CPRINTF_HH__
#define __BASE_CPRINTF_HH__

#include <string>
#include <cstdio>
#include <memory>

namespace gem5
{

inline std::string csprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int sz = vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    if (sz <= 0) return "";
    std::string result(sz + 1, '\0');
    va_start(args, fmt);
    vsnprintf(&result[0], sz + 1, fmt, args);
    va_end(args);
    result.resize(sz);
    return result;
}

inline void ccprintf(std::ostream &os, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int sz = vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    if (sz > 0) {
        std::string result(sz + 1, '\0');
        va_start(args, fmt);
        vsnprintf(&result[0], sz + 1, fmt, args);
        va_end(args);
        result.resize(sz);
        os << result;
    }
}

} // namespace gem5
#endif
