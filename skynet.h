#ifndef SKYNET_H
#define SKYNET_H

#include <stddef.h>

struct skynet_context;

void skynet_error(struct skynet_context * context, const char *msg, ...);
const char * skynet_command(struct skynet_context * context, const char * cmd , const char * parm);
void skynet_send(struct skynet_context * context, const char * addr , void * msg, size_t sz_session);

typedef void (*skynet_cb)(struct skynet_context * context, void *ud, const char * uid , const void * msg, size_t sz_session);
void skynet_callback(struct skynet_context * context, void *ud, skynet_cb cb);

#endif
