#ifndef SKYNET_TIMER_H
#define SKYNET_TIMER_H

#include "skynet_system.h"
#include <stdint.h>

void skynet_timeout(int handle, int time, int session);
void skynet_updatetime(void);
uint32_t skynet_gettime(void);

void skynet_timer_init(void);

#endif
