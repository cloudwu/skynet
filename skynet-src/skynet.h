#ifndef SKYNET_H
#define SKYNET_H

#include <stddef.h>
#include <stdint.h>

#define DONTCOPY 1
#define SESSION_CLIENT 0x7fffffff
#define SESSION_MULTICAST 0x7ffffffe
#define SESSION_MAX 0x7ffffff0

struct skynet_context;

void skynet_error(struct skynet_context * context, const char *msg, ...);
const char * skynet_command(struct skynet_context * context, const char * cmd , const char * parm);
int skynet_send(struct skynet_context * context, const char * source, const char * addr , int session, void * msg, size_t sz, int flags);

void skynet_forward(struct skynet_context *, const char * destination);

typedef int (*skynet_cb)(struct skynet_context * context, void *ud, int session, const char * addr , const void * msg, size_t sz);
void skynet_callback(struct skynet_context * context, void *ud, skynet_cb cb);

#endif
