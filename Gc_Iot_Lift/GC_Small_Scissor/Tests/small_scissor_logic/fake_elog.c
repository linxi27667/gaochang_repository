#include "fake_elog.h"
#include "elog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOG_BUFFER_SIZE 32768U

static char s_log[LOG_BUFFER_SIZE];
static size_t s_log_used;
static size_t s_log_count;

void fake_elog_reset(void)
{
    s_log[0] = '\0';
    s_log_used = 0U;
    s_log_count = 0U;
}

void test_elog_record(const char *level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (s_log_used >= (LOG_BUFFER_SIZE - 1U)) {
        return;
    }

    n = snprintf(s_log + s_log_used,
                 LOG_BUFFER_SIZE - s_log_used,
                 "[%s/%s] ",
                 level,
                 tag);
    if (n < 0) {
        return;
    }
    s_log_used += (size_t)n;
    if (s_log_used >= (LOG_BUFFER_SIZE - 1U)) {
        s_log[LOG_BUFFER_SIZE - 1U] = '\0';
        return;
    }

    va_start(ap, fmt);
    n = vsnprintf(s_log + s_log_used, LOG_BUFFER_SIZE - s_log_used, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    s_log_used += (size_t)n;
    if (s_log_used < (LOG_BUFFER_SIZE - 2U)) {
        s_log[s_log_used++] = '\n';
        s_log[s_log_used] = '\0';
    }
    s_log_count++;
}

int fake_elog_contains(const char *needle)
{
    return (needle != 0 && strstr(s_log, needle) != 0) ? 1 : 0;
}

void fake_elog_dump(void)
{
    printf("%s", s_log);
}

size_t fake_elog_count(void)
{
    return s_log_count;
}
