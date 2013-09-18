#ifndef TRACE_SERVICE_H
#define TRACE_SERVICE_H

#include <time.h>
#include <stdint.h>

#define NANOSEC 1000000000

void current_time(struct timespec *ti);
void diff_time(struct timespec *ti, uint32_t *sec, uint32_t *nsec);

struct trace_pool;
struct trace_info;

struct trace_pool *  trace_create();
void trace_release(struct trace_pool *);
struct trace_info * trace_new(struct trace_pool *);
void trace_register(struct trace_pool *, int session);
void trace_switch(struct trace_pool *, int session);
struct trace_info * trace_yield(struct trace_pool *);
double trace_delete(struct trace_pool *, struct trace_info *);

#endif
