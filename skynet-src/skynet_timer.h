#ifndef SKYNET_TIMER_H
#define SKYNET_TIMER_H

#include <stdint.h>

int skynet_timeout(uint32_t handle, int time, int session);  // ע��
void skynet_updatetime(void);      // ���ϸ���
uint32_t skynet_starttime(void);   // ֻ��������

void skynet_timer_init(void);  // ��ʼ���������ʼ��ʱ��

#endif
