#include "skynet_server.h"
#include "skynet_module.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
#include "skynet_timer.h"
#include "skynet_harbor.h"
#include "skynet_env.h"
#include "skynet.h"
#include "skynet_multicast.h"
#include "skynet_group.h"
#include "skynet_monitor.h"

#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef CALLING_CHECK

#define CHECKCALLING_BEGIN(ctx) assert(__sync_lock_test_and_set(&ctx->calling,1) == 0);
#define CHECKCALLING_END(ctx) __sync_lock_release(&ctx->calling);
#define CHECKCALLING_INIT(ctx) ctx->calling = 0;
#define CHECKCALLING_DECL int calling;

#else

#define CHECKCALLING_BEGIN(ctx)
#define CHECKCALLING_END(ctx)
#define CHECKCALLING_INIT(ctx)
#define CHECKCALLING_DECL

#endif

// skynet 主要功能 加载服务和通知服务

/*
 * 一个模块(.so)加载到skynet框架中，创建出来的一个实例就是一个服务，
 * 为每个服务分配一个skynet_context结构
 */
// 每一个服务对应的 skynet_ctx 结构
struct skynet_context {
	void * instance;			// 模块xxx_create函数返回的实例 对应 模块的句柄
	struct skynet_module * mod;	// 模块
	uint32_t handle;			// 服务句柄
	int ref;					// 线程安全的引用计数，保证在使用的时候，没有被其它线程释放
	char result[32];			// 保存命令执行返回结果
	void * cb_ud;				// 传递给回调函数的参数，一般是xxx_create函数返回的实例
	skynet_cb cb;				// 回调函数
	int session_id;				// 会话id
	uint32_t forward;			// 转发地址(另一个服务句柄)，0不转发 下一个目的 handle
	struct message_queue *queue;	// 消息队列
	bool init;					// 是否初始化
	bool endless;				// 是否无限循环

	CHECKCALLING_DECL
};

// skynet 的节点 结构
struct skynet_node {
	int total;		// 一个skynet_node的服务数 一个 node 的服务数量
	uint32_t monitor_exit;
};

static struct skynet_node G_NODE = { 0,0 };

int 
skynet_context_total() {
	return G_NODE.total;
}

static void
_context_inc() { // increase
	__sync_fetch_and_add(&G_NODE.total,1);
}

static void
_context_dec() { // decrease
	__sync_fetch_and_sub(&G_NODE.total,1);
}

static void
_id_to_hex(char * str, uint32_t id) {
	int i;
	static char hex[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
	str[0] = ':';
	for (i=0;i<8;i++) { // 转成 16 进制的 0xff ff ff ff 8位
		str[i+1] = hex[(id >> ((7-i) * 4)) & 0xf]; // 依次取 4位 从最高的4位 开始取 在纸上画一下就清楚了
	}
	str[9] = '\0';
}

// skynet 新的 ctx
struct skynet_context * 
skynet_context_new(const char * name, const char *param) {
	struct skynet_module * mod = skynet_module_query(name);

	if (mod == NULL)
		return NULL;

	void *inst = skynet_module_instance_create(mod);	// 调用模块创建函数
	if (inst == NULL)
		return NULL;
	struct skynet_context * ctx = malloc(sizeof(*ctx));
	CHECKCALLING_INIT(ctx)

	ctx->mod = mod;
	ctx->instance = inst;
	ctx->ref = 2;
	ctx->cb = NULL;
	ctx->cb_ud = NULL;
	ctx->session_id = 0;

	ctx->forward = 0;
	ctx->init = false;
	ctx->endless = false;
	ctx->handle = skynet_handle_register(ctx);	// 注册，得到一个唯一的句柄
	struct message_queue * queue = ctx->queue = skynet_mq_create(ctx->handle); // mq
	// init function maybe use ctx->handle, so it must init at last

	_context_inc();		// 节点服务数加1

	CHECKCALLING_BEGIN(ctx)

	int r = skynet_module_instance_init(mod, inst, ctx, param);

	CHECKCALLING_END(ctx)

	if (r == 0) {
		struct skynet_context * ret = skynet_context_release(ctx);
		if (ret) {
			ctx->init = true;
		}

		/*
			ctx 的初始化流程是可以发送消息出去的（同时也可以接收到消息），但在初始化流程完成前，
			接收到的消息都必须缓存在 mq 中，不能处理。我用了个小技巧解决这个问题。就是在初始化流程开始前，
			假装 mq 在 globalmq 中（这是由 mq 中一个标记位决定的）。这样，向它发送消息，并不会把它的 mq 压入 globalmq ，
			自然也不会被工作线程取到。等初始化流程结束，在强制把 mq 压入 globalmq （无论是否为空）。即使初始化失败也要进行这个操作。
		*/

		// 初始化流程结构后将这个 ctx 对应的 mq 强制压入 globalmq
		skynet_mq_force_push(queue);
		if (ret) {
			skynet_error(ret, "LAUNCH %s %s", name, param ? param : "");
		}
		return ret;
	}
	else {
		skynet_error(ctx, "FAILED launch %s", name);
		skynet_context_release(ctx);
		skynet_handle_retire(ctx->handle);
		skynet_mq_release(queue);
		return NULL;
	}
}

// 分配一个session id
int
skynet_context_newsession(struct skynet_context *ctx) {
	// session always be a positive number
	int session = (++ctx->session_id) & 0x7fffffff;
	return session;
}

void 
skynet_context_grab(struct skynet_context *ctx) {
	__sync_add_and_fetch(&ctx->ref,1);	// skynet_context引用计数加1
}

/*
	问题就在这里:
		handle 和 ctx 的绑定关系是在 ctx 模块外部操作的（不然也做不到 ctx 的正确销毁），

	无法确保从 handle 确认对应的 ctx 无效的同时，ctx 真的已经被销毁了。
	所以，当工作线程判定 mq 可以销毁时（对应的 handle 无效），ctx 可能还活着（另一个工作线程还持有其引用），
	持有这个 ctx 的工作线程可能正在它生命的最后一刻，向其发送消息。结果 mq 已经销毁了。

	当 ctx 销毁前，由它向其 mq 设入一个清理标记。然后在 globalmq 取出 mq ，发现已经找不到 handle 对应的 ctx 时，
	先判断是否有清理标记。如果没有，再将 mq 重放进 globalmq ，直到清理标记有效，在销毁 mq 。
*/

static void 
_delete_context(struct skynet_context *ctx) {
	skynet_module_instance_release(ctx->mod, ctx->instance);
	skynet_mq_mark_release(ctx->queue); // 设置标记位 并且把它压入 global mq
	free(ctx);
	_context_dec(); // 这个节点对应的服务数也 减 1
}

struct skynet_context * 
skynet_context_release(struct skynet_context *ctx) {
	// 引用计数减1，减为0则删除skynet_context
	if (__sync_sub_and_fetch(&ctx->ref,1) == 0) {
		_delete_context(ctx);
		return NULL;
	}
	return ctx;
}

// 往handle标识的服务中插入一条消息
int
skynet_context_push(uint32_t handle, struct skynet_message *message) {
	struct skynet_context * ctx = skynet_handle_grab(handle);
	if (ctx == NULL) {
		return -1;
	}
	skynet_mq_push(ctx->queue, message);
	skynet_context_release(ctx);

	return 0;
}

void 
skynet_context_endless(uint32_t handle) {
	struct skynet_context * ctx = skynet_handle_grab(handle);
	if (ctx == NULL) {
		return;
	}
	ctx->endless = true;
	skynet_context_release(ctx);
}

int 
skynet_isremote(struct skynet_context * ctx, uint32_t handle, int * harbor) {
	int ret = skynet_harbor_message_isremote(handle);	// 判断是否是远程消息
	if (harbor) {
		*harbor = (int)(handle >> HANDLE_REMOTE_SHIFT);	// 返回harbor(注：高8位存的是harbor) yes
	}
	return ret;
}

// 发送消息
static void
_send_message(uint32_t des, struct skynet_message *msg) {
	if (skynet_harbor_message_isremote(des)) {
			struct remote_message * rmsg = malloc(sizeof(*rmsg));
			rmsg->destination.handle = des;
			rmsg->message = msg->data;
			rmsg->sz = msg->sz;
			// 如果是远程消息，先发送给本节点对应的harbor服务
			skynet_harbor_send(rmsg, msg->source, msg->session);
	} else {
		if (skynet_context_push(des, msg)) {
			free(msg->data);
			skynet_error(NULL, "Drop message from %x forward to %x (size=%d)", msg->source, des, (int)msg->sz);
		}
	}
}

// 转发消息
static int
_forwarding(struct skynet_context *ctx, struct skynet_message *msg) {
	if (ctx->forward) {
		uint32_t des = ctx->forward;
		ctx->forward = 0;
		_send_message(des, msg);
		return 1;
	}
	return 0;
}

static void
_mc(void *ud, uint32_t source, const void * msg, size_t sz) {
	struct skynet_context * ctx = ud;
	int type = sz >> HANDLE_REMOTE_SHIFT;
	sz &= HANDLE_MASK;
	ctx->cb(ctx, ctx->cb_ud, type, 0, source, msg, sz);
	if (ctx->forward) {
		uint32_t des = ctx->forward;
		ctx->forward = 0;
		struct skynet_message message;
		message.source = source;
		message.session = 0;
		message.data = malloc(sz);
		memcpy(message.data, msg, sz);
		message.sz = sz  | (type << HANDLE_REMOTE_SHIFT);
		_send_message(des, &message);
	}
}

static void
_dispatch_message(struct skynet_context *ctx, struct skynet_message *msg) {
	assert(ctx->init);
	CHECKCALLING_BEGIN(ctx)
	int type = msg->sz >> HANDLE_REMOTE_SHIFT;	// 高8位存消息类别
	size_t sz = msg->sz & HANDLE_MASK;			// 低24位消息大小
	if (type == PTYPE_MULTICAST) {
		skynet_multicast_dispatch((struct skynet_multicast_message *)msg->data, ctx, _mc);
	} else {
		// 返回1则不会释放msg->data,通常发送的消息是DONCOPY的，则回调函数返回1
		int reserve = ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz);
		// 如果消息被转发，也不需要释放msg->data
		reserve |= _forwarding(ctx, msg);
		if (!reserve) {
			free(msg->data);
		}
	}
	CHECKCALLING_END(ctx)
}

int
skynet_context_message_dispatch(struct skynet_monitor *sm) {
	struct message_queue * q = skynet_globalmq_pop();	// 弹出一个消息队列
	if (q==NULL)
		return 1;

	uint32_t handle = skynet_mq_handle(q);	// 得到消息队列所属的服务句柄

	struct skynet_context * ctx = skynet_handle_grab(handle);	// 根据handle获取skynet_context
	if (ctx == NULL) {
		int s = skynet_mq_release(q);
		if (s>0) {
			skynet_error(NULL, "Drop message queue %x (%d messages)", handle,s);
		}
		return 0;
	}

	struct skynet_message msg;
	// 返回1，表示消息队列q中没有消息
	if (skynet_mq_pop(q,&msg)) {
		skynet_context_release(ctx);
		return 0;
	}

	skynet_monitor_trigger(sm, msg.source , handle); // 消息处理完，调用该函数，以便监控线程知道该消息已处理

	if (ctx->cb == NULL) {
		free(msg.data);
		skynet_error(NULL, "Drop message from %x to %x without callback , size = %d",msg.source, handle, (int)msg.sz);
	}
	else {
		_dispatch_message(ctx, &msg);
	}

	assert(q == ctx->queue);
	skynet_mq_pushglobal(q);
	skynet_context_release(ctx);

	skynet_monitor_trigger(sm, 0,0);

	return 0;
}

static void
_copy_name(char name[GLOBALNAME_LENGTH], const char * addr) {
	int i;
	for (i=0;i<GLOBALNAME_LENGTH && addr[i];i++) {
		name[i] = addr[i];
	}
	for (;i<GLOBALNAME_LENGTH;i++) {
		name[i] = '\0';
	}
}

static const char *
_group_command(struct skynet_context * ctx, const char * cmd, int handle, uint32_t v) {
	uint32_t self;
	if (v != 0) {
		if (skynet_harbor_message_isremote(v)) {
			skynet_error(ctx, "Can't add remote handle %x",v);
			return NULL;
		}
		self = v;
	} else {
		self = ctx->handle;
	}
	if (strcmp(cmd, "ENTER") == 0) {
		skynet_group_enter(handle, self);
		return NULL;
	}
	if (strcmp(cmd, "LEAVE") == 0) {
		skynet_group_leave(handle, self);
		return NULL;
	}
	if (strcmp(cmd, "QUERY") == 0) {
		uint32_t addr = skynet_group_query(handle);
		if (addr == 0) {
			return NULL;
		}
		_id_to_hex(ctx->result, addr);
		return ctx->result;
	}
	if (strcmp(cmd, "CLEAR") == 0) {
		skynet_group_clear(handle);
		return NULL;
	}
	return NULL;
}

uint32_t 
skynet_queryname(struct skynet_context * context, const char * name) {
	switch(name[0]) {
	case ':':
		return strtoul(name+1,NULL,16);
	case '.':
		return skynet_handle_findname(name + 1);
	}
	skynet_error(context, "Don't support query global name %s",name);
	return 0;
}

static void
handle_exit(struct skynet_context * context, uint32_t handle) {
	if (handle == 0) {
		handle = context->handle;
		skynet_error(context, "KILL self");
	}
	else {
		skynet_error(context, "KILL :%0x", handle);
	}
	if (G_NODE.monitor_exit) {
		skynet_send(context,  handle, G_NODE.monitor_exit, PTYPE_CLIENT, 0, NULL, 0);
	}
	skynet_handle_retire(handle); // 回收这个 handle
}

// 使用了简单的文本协议 来 cmd 操作 skynet的服务
/*
 * skynet 提供了一个叫做 skynet_command 的 C API ，作为基础服务的统一入口。
 * 它接收一个字符串参数，返回一个字符串结果。你可以看成是一种文本协议。
 * 但 skynet_command 保证在调用过程中，不会切出当前的服务线程，导致状态改变的不可预知性。
 * 其每个功能的实现，其实也是内嵌在 skynet 的源代码中，相同上层服务，还是比较高效的。
 *（因为可以访问许多内存 api ，而不必用消息通讯的方式实现）
 */
const char * 
skynet_command(struct skynet_context * context, const char * cmd , const char * param) {
	if (strcmp(cmd,"TIMEOUT") == 0) {
		char * session_ptr = NULL;
		int ti = strtol(param, &session_ptr, 10);
		int session = skynet_context_newsession(context); // new session_id

		skynet_timeout(context->handle, ti, session); // 插入定时器 单位是 0.01s
		sprintf(context->result, "%d", session);
		return context->result;
	}

	if (strcmp(cmd,"LOCK") == 0) {
		if (context->init == false) {
			return NULL;
		}
		skynet_mq_lock(context->queue, context->session_id+1);
		return NULL;
	}

	if (strcmp(cmd,"UNLOCK") == 0) {
		if (context->init == false) {
			return NULL;
		}
		skynet_mq_unlock(context->queue);
		return NULL;
	}

	// 处理 name 得到自己的 addr REG命令
	if (strcmp(cmd,"REG") == 0) {
		if (param == NULL || param[0] == '\0') {
			sprintf(context->result, ":%x", context->handle);
			return context->result;
		}
		else if (param[0] == '.') {
			return skynet_handle_namehandle(context->handle, param + 1);
		}
		else {
			assert(context->handle!=0);
			struct remote_name *rname = malloc(sizeof(*rname));
			_copy_name(rname->name, param);
			rname->handle = context->handle;
			skynet_harbor_register(rname);
			return NULL;
		}
	}

	// find hanlde by name
	if (strcmp(cmd,"QUERY") == 0) {
		if (param[0] == '.') {
			uint32_t handle = skynet_handle_findname(param+1);
			sprintf(context->result, ":%x", handle);
			return context->result;
		}
		return NULL;
	}

	// skynet_handle_namehandle()
	// skynet_harbor_register()
	if (strcmp(cmd,"NAME") == 0) {
		int size = strlen(param);
		char name[size+1];
		char handle[size+1];

		sscanf(param,"%s %s",name,handle);
		if (handle[0] != ':') {
			return NULL;
		}

		uint32_t handle_id = strtoul(handle+1, NULL, 16);
		if (handle_id == 0) {
			return NULL;
		}

		// .开头的都是本地的 hanlde - name 即本地服务
		if (name[0] == '.') {
			return skynet_handle_namehandle(handle_id, name + 1);
		}
		// 远程的服务
		else {
			struct remote_name *rname = malloc(sizeof(*rname));
			_copy_name(rname->name, name);
			rname->handle = handle_id;
			skynet_harbor_register(rname);
		}
		return NULL;
	}

	if (strcmp(cmd,"NOW") == 0) {
		uint32_t ti = skynet_gettime();
		sprintf(context->result,"%u",ti);
		return context->result;
	}

	if (strcmp(cmd,"EXIT") == 0) {
		handle_exit(context, 0);
		return NULL;
	}

	// 杀死某个 handle
	if (strcmp(cmd,"KILL") == 0) {
		uint32_t handle = 0;
		if (param[0] == ':') {
			handle = strtoul(param+1, NULL, 16);
		} else if (param[0] == '.') {
			handle = skynet_handle_findname(param+1);
		} else {
			skynet_error(context, "Can't kill %s",param);
			// todo : kill global service
		}
		if (handle) {
			handle_exit(context, handle);
		}
		return NULL;
	}

	// launch
	if (strcmp(cmd,"LAUNCH") == 0) {
		size_t sz = strlen(param);
		char tmp[sz+1];
		strcpy(tmp,param);
		char * args = tmp;
		char * mod = strsep(&args, " \t\r\n");
		args = strsep(&args, "\r\n");
		struct skynet_context * inst = skynet_context_new(mod,args);
		if (inst == NULL) {
			return NULL;
		} else {
			_id_to_hex(context->result, inst->handle);
			return context->result;
		}
	}

	if (strcmp(cmd,"GETENV") == 0) {
		return skynet_getenv(param);
	}

	if (strcmp(cmd,"SETENV") == 0) {
		size_t sz = strlen(param);
		char key[sz+1];
		int i;
		for (i=0;param[i] != ' ' && param[i];i++) {
			key[i] = param[i];
		}
		if (param[i] == '\0')
			return NULL;

		key[i] = '\0';
		param += i+1;
		
		skynet_setenv(key,param);
		return NULL;
	}

	// start time 开机时间
	if (strcmp(cmd,"STARTTIME") == 0) {
		uint32_t sec = skynet_gettime_fixsec();
		sprintf(context->result,"%u",sec);
		return context->result;
	}

	// group
	if (strcmp(cmd,"GROUP") == 0) {
		int sz = strlen(param);
		char tmp[sz+1];
		strcpy(tmp,param);
		tmp[sz] = '\0';
		char cmd[sz+1];
		int handle=0;
		uint32_t addr=0;
		sscanf(tmp, "%s %d :%x",cmd,&handle,&addr);
		return _group_command(context, cmd, handle,addr);
	}

    // endless 无限循环
	if (strcmp(cmd,"ENDLESS") == 0) {
		if (context->endless) {
			strcpy(context->result, "1");
			context->endless = false;
			return context->result;
		}
		return NULL;
	}

	// abort 回收所有的 handle 即取消所有的 服务
	if (strcmp(cmd,"ABORT") == 0) {
		skynet_handle_retireall();
		return NULL;
	}

	// monitor 监控
	if (strcmp(cmd,"MONITOR") == 0) {
		uint32_t handle=0;
		if (param == NULL || param[0] == '\0') {
			if (G_NODE.monitor_exit) {
				// return current monitor serivce
				sprintf(context->result, ":%x", G_NODE.monitor_exit);
				return context->result;
			}
			return NULL;
		} else {
			if (param[0] == ':') {
				handle = strtoul(param+1, NULL, 16);
			} else if (param[0] == '.') {
				handle = skynet_handle_findname(param+1);
			} else {
				skynet_error(context, "Can't monitor %s",param);
				// todo : monitor global service
			}
		}
		G_NODE.monitor_exit = handle;
		return NULL;
	}

	// mq_len
	if (strcmp(cmd, "MQLEN") == 0) {
		int len = skynet_mq_length(context->queue);
		sprintf(context->result, "%d", len);
		return context->result;
	}

	return NULL;
}

// 设置转发地址 即设置 forward的目的地址
void 
skynet_forward(struct skynet_context * context, uint32_t destination) {
	assert(context->forward == 0);
	if (destination == 0) {
		context->forward = context->handle; // 0: 不转发 就是本 ctx对应的 handle
	} else {
		context->forward = destination;
	}
}

// 参数过滤 没看懂
static void
_filter_args(struct skynet_context * context, int type, int *session, void ** data, size_t * sz) {
	int needcopy = !(type & PTYPE_TAG_DONTCOPY);
	int allocsession = type & PTYPE_TAG_ALLOCSESSION; // type中含有 PTYPE_TAG_ALLOCSESSION ，则session必须是0
	type &= 0xff;

	if (allocsession) {
		assert(*session == 0);
		*session = skynet_context_newsession(context); // 分配一个新的 session id
	}

	if (needcopy && *data) {
		char * msg = malloc(*sz+1);
		memcpy(msg, *data, *sz);
		msg[*sz] = '\0';
		*data = msg;
	}

	assert((*sz & HANDLE_MASK) == *sz);
	*sz |= type << HANDLE_REMOTE_SHIFT;
}

/*
 * 向handle为destination的服务发送消息(注：handle为destination的服务不一定是本地的)
 * type中含有 PTYPE_TAG_ALLOCSESSION ，则session必须是0
 * type中含有 PTYPE_TAG_DONTCOPY ，则不需要拷贝数据
 */
int
skynet_send(struct skynet_context * context, uint32_t source, uint32_t destination , int type, int session, void * data, size_t sz) {
	_filter_args(context, type, &session, (void **)&data, &sz);

	if (source == 0) {
		source = context->handle;
	}

	if (destination == 0) {
		return session;
	}

	// 如果消息时发给远程的
	if (skynet_harbor_message_isremote(destination)) {
		struct remote_message * rmsg = malloc(sizeof(*rmsg));
		rmsg->destination.handle = destination;
		rmsg->message = data;
		rmsg->sz = sz;
		skynet_harbor_send(rmsg, source, session);
	}
	// 本机消息 直接压入消息队列
	else {
		struct skynet_message smsg;
		smsg.source = source;
		smsg.session = session;
		smsg.data = data;
		smsg.sz = sz;

		if (skynet_context_push(destination, &smsg)) {
			free(data);
			skynet_error(NULL, "Drop message from %x to %x (type=%d)(size=%d)", source, destination, type&0xff, (int)(sz & HANDLE_MASK));
			return -1;
		}
	}
	return session;
}

// sendname 干吗的？
int
skynet_sendname(struct skynet_context * context, const char * addr , int type, int session, void * data, size_t sz) {
	uint32_t source = context->handle;
	uint32_t des = 0;
	if (addr[0] == ':') {
		des = strtoul(addr+1, NULL, 16); // strtoul （将字符串转换成无符号长整型数） 如果开始时 :2343 这种形式的 说明就是直接的 handle
	}
	else if (addr[0] == '.') { // . 说明是以名字开始的地址 需要根据名字查找 对应的 handle
		des = skynet_handle_findname(addr + 1); // 根据名称查找对应的 handle
		if (des == 0) {
			if (type & PTYPE_TAG_DONTCOPY) { // 不需要拷贝的消息类型
  			free(data);
			}
			skynet_error(context, "Drop message to %s", addr);
			return session;
		}
	}
	else { // 其他的目的地址 即远程的地址
		_filter_args(context, type, &session, (void **)&data, &sz);

		struct remote_message * rmsg = malloc(sizeof(*rmsg));
		_copy_name(rmsg->destination.name, addr);
		rmsg->destination.handle = 0;
		rmsg->message = data;
		rmsg->sz = sz;

		skynet_harbor_send(rmsg, source, session); // 发送给 harbor 去处理远程的消息
		return session;
	}

	return skynet_send(context, source, des, type, session, data, sz);
}

uint32_t 
skynet_context_handle(struct skynet_context *ctx) {
	return ctx->handle;
}

void 
skynet_context_init(struct skynet_context *ctx, uint32_t handle) {
	ctx->handle = handle;
}

void 
skynet_callback(struct skynet_context * context, void *ud, skynet_cb cb) {
	context->cb = cb;
	context->cb_ud = ud;
}

// 向ctx服务发送消息(注：ctx服务一定是本地的)
void
skynet_context_send(struct skynet_context * ctx, void * msg, size_t sz, uint32_t source, int type, int session) {
	struct skynet_message smsg;
	smsg.source = source;
	smsg.session = session;
	smsg.data = msg;
	smsg.sz = sz | type << HANDLE_REMOTE_SHIFT; // 这里还是有点没问 为什么 这个sz  是这样的

	skynet_mq_push(ctx->queue, &smsg); // 压入消息队列
}
