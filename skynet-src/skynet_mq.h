#ifndef SKYNET_MESSAGE_QUEUE_H
#define SKYNET_MESSAGE_QUEUE_H

#include <stdlib.h>
#include <stdint.h>

struct skynet_message {
	uint32_t source;   //消息所属服务的地址（服务实例的 handle）
	int session;       //用来做上下文的标识
	void * data;       //消息地址指针
	size_t sz;         //消息大小（消息的请求类型定义在高8位）
};

// type is encoding in skynet_message.sz high 8bit
#define MESSAGE_TYPE_MASK (SIZE_MAX >> 8)
#define MESSAGE_TYPE_SHIFT ((sizeof(size_t)-1) * 8)

struct message_queue;

void skynet_globalmq_push(struct message_queue * queue);
struct message_queue * skynet_globalmq_pop(void);

struct message_queue * skynet_mq_create(uint32_t handle);
void skynet_mq_mark_release(struct message_queue *q);

typedef void (*message_drop)(struct skynet_message *, void *);

void skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud);
uint32_t skynet_mq_handle(struct message_queue *);

// 0 for success
int skynet_mq_pop(struct message_queue *q, struct skynet_message *message);
void skynet_mq_push(struct message_queue *q, struct skynet_message *message);

// return the length of message queue, for debug
int skynet_mq_length(struct message_queue *q);
int skynet_mq_overload(struct message_queue *q);

void skynet_mq_init();

#endif
