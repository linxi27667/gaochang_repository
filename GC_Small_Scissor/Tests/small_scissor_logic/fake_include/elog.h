#ifndef TEST_ELOG_H
#define TEST_ELOG_H

#include <stdint.h>

#define ELOG_LVL_ASSERT 0
#define ELOG_LVL_ERROR  1
#define ELOG_LVL_WARN   2
#define ELOG_LVL_INFO   3
#define ELOG_LVL_DEBUG  4

void test_elog_record(const char *level, const char *tag, const char *fmt, ...);

#define elog_e(tag, ...) test_elog_record("E", tag, __VA_ARGS__)
#define elog_i(tag, ...) test_elog_record("I", tag, __VA_ARGS__)
#define elog_w(tag, ...) test_elog_record("W", tag, __VA_ARGS__)
#define elog_d(tag, ...) test_elog_record("D", tag, __VA_ARGS__)

#endif
