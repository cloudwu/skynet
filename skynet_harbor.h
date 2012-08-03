#ifndef SKYNET_HARBOR_H
#define SKYNET_HARBOR_H

#include <stdint.h>

struct skynet_message;

void skynet_harbor_send(const char *name, struct skynet_message * message);
void skynet_harbor_register(const char *name, uint32_t handle);

// remote message is diffrent from local message.
// We must use these api to open and close message , see skynet_server.c
int skynet_harbor_message_isremote(uint32_t handle);
void * skynet_harbor_message_open(struct skynet_message * message);
void skynet_harbor_message_close(struct skynet_message * message);

// harbor worker thread
void * skynet_harbor_dispatch_thread(void *ud);
void skynet_harbor_init(const char * master, const char *local, int harbor);

#endif
