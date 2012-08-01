#ifndef SKYNET_CONTEXT_HANDLE_H
#define SKYNET_CONTEXT_HANDLE_H

struct skynet_context;

int skynet_handle_register(struct skynet_context *);
void skynet_handle_retire(int handle);
struct skynet_context * skynet_handle_grab(int handle);

int skynet_handle_findname(const char * name);
const char * skynet_handle_namehandle(int handle, const char *name);

void skynet_handle_init(void);

#endif
