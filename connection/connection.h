#ifndef SKYNET_CONNECTION_H
#define SKYNET_CONNECTION_H

#include <stddef.h>

struct connection_pool;

struct connection_pool * connection_newpool(int max);
void connection_deletepool(struct connection_pool *);

int connection_open(struct connection_pool *, const char * ipaddr);
void connection_close(struct connection_pool *, int handle);

void * connection_read(struct connection_pool *, int handle, size_t sz);
void * connection_readline(struct connection_pool *, int handle, const char * sep, size_t *sz);

void * connection_poll(struct connection_pool *, int timeout, int *handle, size_t *sz);
int connection_closed(struct connection_pool *, int handle);

void connection_write(struct connection_pool *, int handle, const void * buffer, size_t sz);
int connection_id(struct connection_pool *, int handle);

#endif
