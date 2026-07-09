#ifndef TEST_ASSERT_H
#define TEST_ASSERT_H

#include <stdio.h>

typedef struct {
    int passed;
    int failed;
} test_stats_t;

#define TEST_EXPECT(stats, expr, fmt, ...)                                      \
    do {                                                                        \
        if (expr) {                                                             \
            (stats)->passed++;                                                  \
        } else {                                                                \
            (stats)->failed++;                                                  \
            printf("[FAIL] " fmt "\n", ##__VA_ARGS__);                         \
        }                                                                       \
    } while (0)

#endif
