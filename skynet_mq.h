#ifndef SKYNET_MESSAGE_QUEUE_H
#define SKYNET_MESSAGE_QUEUE_H

#include <stdlib.h>
#include <stdint.h>

struct skynet_message {
	uint32_t source;
	uint32_t destination;
	void * data;
	size_t sz;
};

struct message_queue;

uint32_t skynet_mq_pop(struct skynet_message *message);
void skynet_mq_push(struct skynet_message *message);

struct message_queue * skynet_mq_create(int cap);
void skynet_mq_release(struct message_queue *q);
uint32_t skynet_mq_leave(struct message_queue *q, struct skynet_message *message);
void skynet_mq_enter(struct message_queue *q, struct skynet_message *message);

void skynet_mq_init(int cap);

#endif
