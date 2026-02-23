/*
 * rom/ets_sys.h -- ROM utility stubs
 */
#ifndef ROM_ETS_SYS_H
#define ROM_ETS_SYS_H

#include <unistd.h>

static inline void ets_delay_us(unsigned int us)
{
    usleep(us);
}

#endif /* ROM_ETS_SYS_H */
