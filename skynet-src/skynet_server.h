#ifndef SKYNET_SERVER_H
#define SKYNET_SERVER_H

#include <stdint.h>
#include <stdlib.h>

struct skynet_context;
struct skynet_message;
struct skynet_monitor;

// 这个函数
// 应该说一个每个skynet_context就是我们理解的一个service，每个skynet_context还对应着一个skynet_module
// 这里的name参数就是文件名，对应这个那个.so文件
struct skynet_context * skynet_context_new(const char * name, const char * parm);  
void skynet_context_grab(struct skynet_context *);
void skynet_context_reserve(struct skynet_context *ctx);
struct skynet_context * skynet_context_release(struct skynet_context *);
uint32_t skynet_context_handle(struct skynet_context *);
int skynet_context_push(uint32_t handle, struct skynet_message *message);
void skynet_context_send(struct skynet_context * context, void * msg, size_t sz, uint32_t source, int type, int session);
int skynet_context_newsession(struct skynet_context *);
struct message_queue * skynet_context_message_dispatch(struct skynet_monitor *, struct message_queue *, int weight);	// return next queue
int skynet_context_total();
void skynet_context_dispatchall(struct skynet_context * context);	// for skynet_error output before exit

void skynet_context_endless(uint32_t handle);	// for monitor

// 此三个函数主要用于最开始的初始化
void skynet_globalinit(void);
void skynet_globalexit(void);
void skynet_initthread(int m);

#endif
