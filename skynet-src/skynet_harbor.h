#ifndef SKYNET_HARBOR_H
#define SKYNET_HARBOR_H

#include <stdint.h>
#include <stdlib.h>

#define GLOBALNAME_LENGTH 16 // 全局名字的长度
#define REMOTE_MAX 256

// reserve high 8 bits for remote id
#define HANDLE_MASK 0xffffff   // 24 bits 这里的 handle 只使用了 低  24 位  高  8 位留给远程服务使用 可能在分布式系统中会用到
#define HANDLE_REMOTE_SHIFT 24 // 远程 id 需要偏移 24位得到

// 远程服务名和对应的handle
struct remote_name {
	char name[GLOBALNAME_LENGTH];
	uint32_t handle;
};

// 远程消息
struct remote_message {
	struct remote_name destination;
	const void * message;
	size_t sz;
};

// 向远程服务发送消息
void skynet_harbor_send(struct remote_message *rmsg, uint32_t source, int session);

// 向master注册服务名 master用来统一管理所以节点
void skynet_harbor_register(struct remote_name *rname);

int skynet_harbor_message_isremote(uint32_t handle);

void skynet_harbor_init(int harbor);

int skynet_harbor_start(const char * master, const char *local);

#endif
