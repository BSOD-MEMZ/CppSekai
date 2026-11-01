#include "Log.hpp"

#include <cstdarg>
#include <cstdio>

namespace platform
{
namespace
{
// 全开：现有行为不变，用户自己去设置里关。
unsigned gLogMask = (1u << LogCatCount) - 1u;
}

void setLogMask(unsigned mask)
{
    gLogMask = mask;
}

unsigned logMask()
{
    return gLogMask;
}

bool logOn(LogCat cat)
{
    if (cat < 0 || cat >= LogCatCount) {
        return true;
    }
    return (gLogMask & (1u << cat)) != 0u;
}

void logf(LogCat cat, const char* fmt, ...)
{
    if (!logOn(cat) || fmt == nullptr) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stdout, fmt, args);
    va_end(args);
}
}
