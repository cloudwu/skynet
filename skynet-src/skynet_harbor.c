#include "skynet.h"
#include "skynet_harbor.h"
#include "skynet_server.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

// harbor 用来与远程主机通信 master 统一来管理
// http://blog.codingnow.com/2012/09/the_design_of_skynet.html
// 这个是 skynet的设计综述 讲述了 session和 type的作用

static struct skynet_context * REMOTE = 0;		// harbor 服务对应的 skynet_context 指针
static unsigned int HARBOR = 0;

void 
skynet_harbor_send(struct remote_message *rmsg, uint32_t source, int session) {
	int type = rmsg->sz >> HANDLE_REMOTE_SHIFT; // 高  8 bite 用于保存 type
	rmsg->sz &= HANDLE_MASK;
	assert(type != PTYPE_SYSTEM && type != PTYPE_HARBOR);
	skynet_context_send(REMOTE, rmsg, sizeof(*rmsg) , source, type , session);
}

// 向  master 注册
void 
skynet_harbor_register(struct remote_name *rname) {
	int i;
	int number = 1;
	for (i=0; i<GLOBALNAME_LENGTH; i++) {
		char c = rname->name[i];
		if (!(c >= '0' && c <='9')) { // 确保远程主机名字在不在0-9 范围内
			number = 0;
			break;
		}
	}

	assert(number == 0);
	skynet_context_send(REMOTE, rname, sizeof(*rname), 0, PTYPE_SYSTEM , 0);
}

int 
skynet_harbor_message_isremote(uint32_t handle) { // 判断消息是不是来自远程主机的
	int h = (handle & ~HANDLE_MASK); // 取高8位
	return h != HARBOR && h !=0;
}

void
skynet_harbor_init(int harbor) {
	HARBOR = (unsigned int)harbor << HANDLE_REMOTE_SHIFT; // 高8位就是对应远程主机通信的 harbor
}

int
skynet_harbor_start(const char * master, const char *local) {
	size_t sz = strlen(master) + strlen(local) + 32;
	char args[sz];
	sprintf(args, "%s %s %d",master,local,HARBOR >> HANDLE_REMOTE_SHIFT);
	struct skynet_context * inst = skynet_context_new("harbor",args);
	if (inst == NULL) {
		return 1;
	}
	REMOTE = inst;

	return 0;
}
