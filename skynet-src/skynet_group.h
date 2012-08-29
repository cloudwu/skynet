#ifndef SKYNET_GROUP_H
#define SKYNET_GROUP_H

#include <stdint.h>

uint32_t skynet_group_query(int handle);
void skynet_group_enter(int handle, uint32_t node);
void skynet_group_leave(int handle, uint32_t node);
void skynet_group_clear(int handle);

void skynet_group_init();

#endif
