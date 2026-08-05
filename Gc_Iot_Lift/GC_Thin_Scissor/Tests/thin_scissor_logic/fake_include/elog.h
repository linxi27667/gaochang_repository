#ifndef FAKE_ELOG_H
#define FAKE_ELOG_H

void elog_i(const char *tag, const char *fmt, ...);
void elog_w(const char *tag, const char *fmt, ...);
void elog_e(const char *tag, const char *fmt, ...);

#endif
