#include "skynet.h"

#include "skynet_monitor.h"
#include "skynet_server.h"
#include "skynet.h"
#include "atomic.h"

#include <stdlib.h>
#include <string.h>

struct skynet_monitor {
	ATOM_INT version;		// 消耗的消息的编号
	int check_version;		// 用于检查的编号，在一个消耗周期内，编号应当发生变化，否则则可能是消息堵塞
	uint32_t source;		// 当前消耗消息的来源
	uint32_t destination;	// 当前消耗消息投送的目的地，此目的地可能为空。
};

struct skynet_monitor * 
skynet_monitor_new() {
	struct skynet_monitor * ret = skynet_malloc(sizeof(*ret));
	memset(ret, 0, sizeof(*ret));
	return ret;
}

void 
skynet_monitor_delete(struct skynet_monitor *sm) {
	skynet_free(sm);
}

void 
skynet_monitor_trigger(struct skynet_monitor *sm, uint32_t source, uint32_t destination) {
	sm->source = source;
	sm->destination = destination;
	ATOM_FINC(&sm->version);
}

void 
skynet_monitor_check(struct skynet_monitor *sm) {
	if (sm->version == sm->check_version) {
		if (sm->destination) {
			// 标记这个服务可能发生了死循环，并且进行了release（释放？表现上并不是）
			skynet_context_endless(sm->destination);
			skynet_error(NULL, "A message from [ :%08x ] to [ :%08x ] maybe in an endless loop (version = %d)", sm->source , sm->destination, sm->version);
		}
	} else {
		// 如果消息正常消耗，则切换当前编号
		sm->check_version = sm->version;
	}
}
