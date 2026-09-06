#ifndef RISE_STATS_TEST_ASSERT_H
#define RISE_STATS_TEST_ASSERT_H

#include <stdio.h>

typedef struct {
    int passed;
    int failed;
} test_stats_t;

#define TEST_EXPECT(stats, condition, ...) \
    do { \
        if (condition) { \
            (stats)->passed++; \
        } else { \
            (stats)->failed++; \
            printf("  FAIL: "); \
            printf(__VA_ARGS__); \
            printf("\\n"); \
        } \
    } while (0)

#endif
