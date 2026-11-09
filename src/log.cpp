#include "log.h"
#include "plugin.h"

#include <cstdarg>
#include <cstdio>

void Log(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    META_CONPRINTF("[%s] %s\n", PLUGIN_LOG_TAG, buf);
}
