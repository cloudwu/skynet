#ifndef SKYNET_H
#define SKYNET_H

#include <stddef.h>
#include <stdint.h>

#define DONTCOPY 1

struct skynet_context;

void skynet_error(struct skynet_context * context, const char *msg, ...);
const char * skynet_command(struct skynet_context * context, const char * cmd , const char * parm);
int skynet_send(struct skynet_context * context, const char * addr , int session, void * msg, size_t sz, int flags);

typedef void (*skynet_cb)(struct skynet_context * context, void *ud, int session, const char * addr , const void * msg, size_t sz);
void skynet_callback(struct skynet_context * context, void *ud, skynet_cb cb);

#endif
