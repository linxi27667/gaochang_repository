#ifndef TEST_FAKE_ELOG_H
#define TEST_FAKE_ELOG_H

#include <stddef.h>

void fake_elog_reset(void);
int fake_elog_contains(const char *needle);
void fake_elog_dump(void);
size_t fake_elog_count(void);

#endif
