#ifndef SKYNET_TIMER_H
#define SKYNET_TIMER_H

#include <stdint.h>

int skynet_timeout(uint32_t handle, int time, int session);
void skynet_updatetime(void);
uint32_t skynet_gettime(void);
uint32_t skynet_gettime_fixsec(void);

void skynet_timer_init(void);

#endif
