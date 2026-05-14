#ifndef SIM_ELOG_H
#define SIM_ELOG_H

/* EasyLogger is disabled in simulation */

#define ELOG_LVL_ASSERT  0
#define ELOG_LVL_ERROR   1
#define ELOG_LVL_WARN    2
#define ELOG_LVL_INFO    3
#define ELOG_LVL_DEBUG   4

#define ELOG_FMT_LVL    0x01
#define ELOG_FMT_TAG    0x02
#define ELOG_FMT_TIME   0x04
#define ELOG_FMT_FUNC   0x08
#define ELOG_FMT_ALL    0xFF

void elog_init(void);
void elog_start(void);
void elog_set_fmt(int lvl, int fmt);

#endif
