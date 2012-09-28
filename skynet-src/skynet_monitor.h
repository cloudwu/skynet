#ifndef SKYNET_MONITOR_H
#define SKYNET_MONITOR_H

#include <stdint.h>

struct skynet_monitor;

struct skynet_monitor * skynet_monitor_new();
void skynet_monitor_delete(struct skynet_monitor *);
void skynet_monitor_trigger(struct skynet_monitor *, uint32_t source, uint32_t destination);
void skynet_monitor_check(struct skynet_monitor *);

#endif
