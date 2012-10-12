#ifndef SKYNET_MULTICAST_H
#define SKYNET_MULTICAST_H

#include <stddef.h>
#include <stdint.h>

struct skynet_multicast_message;
struct skynet_multicast_group;
struct skynet_context;

typedef void (*skynet_multicast_func)(void *ud, uint32_t source, const void * msg, size_t sz);

struct skynet_multicast_message * skynet_multicast_create(const void * msg, size_t sz, uint32_t source);
void skynet_multicast_copy(struct skynet_multicast_message *, int copy);
void skynet_multicast_dispatch(struct skynet_multicast_message * msg, void * ud, skynet_multicast_func func);
void skynet_multicast_cast(struct skynet_context * from, struct skynet_multicast_message *msg, const uint32_t *dests, int n);

struct skynet_multicast_group * skynet_multicast_newgroup();
void skynet_multicast_deletegroup(struct skynet_multicast_group * group);
void skynet_multicast_entergroup(struct skynet_multicast_group * group, uint32_t handle);
void skynet_multicast_leavegroup(struct skynet_multicast_group * group, uint32_t handle);
int skynet_multicast_castgroup(struct skynet_context * from, struct skynet_multicast_group * group, struct skynet_multicast_message *msg);

#endif
