#ifndef SKYNET_CONNECTION_H
#define SKYNET_CONNECTION_H

#include <stddef.h>

struct connection_pool;

struct connection_pool * connection_newpool(int max);
void connection_deletepool(struct connection_pool *);

int connection_add(struct connection_pool *, int fd, void *ud);
void connection_del(struct connection_pool *, int fd);

void * connection_poll(struct connection_pool *, int timeout);

#endif
