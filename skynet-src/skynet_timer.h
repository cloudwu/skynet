#ifndef SKYNET_TIMER_H
#define SKYNET_TIMER_H

#include <stdint.h>

int skynet_timeout(uint32_t handle, int time, int session);  // 注册
void skynet_updatetime(void);      // 不断更新
uint32_t skynet_starttime(void);   // 只是用来查

void skynet_timer_init(void);  // 初始化，程序最开始的时候

#endif
