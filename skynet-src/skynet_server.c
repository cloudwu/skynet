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

struct skynet_context {
	void * instance;
	struct skynet_module * mod;
	uint32_t handle;
	int ref;
	char result[32];
	void * cb_ud;
	skynet_cb cb;
	int session_id;
	uint32_t forward;
	struct message_queue *queue;
	bool init;
	bool endless;

	CHECKCALLING_DECL
};

static int g_total_context = 0;

int 
skynet_context_total() {
	return g_total_context;
}

static void
_context_inc() {
	__sync_fetch_and_add(&g_total_context,1);
}

static void
_context_dec() {
	__sync_fetch_and_sub(&g_total_context,1);
}

static void
_id_to_hex(char * str, uint32_t id) {
	int i;
	static char hex[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
	str[0] = ':';
	for (i=0;i<8;i++) {
		str[i+1] = hex[(id >> ((7-i) * 4))&0xf];
	}
	str[9] = '\0';
}

struct skynet_context * 
skynet_context_new(const char * name, const char *param) {
	struct skynet_module * mod = skynet_module_query(name);

	if (mod == NULL)
		return NULL;

	void *inst = skynet_module_instance_create(mod);
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
	ctx->handle = skynet_handle_register(ctx);
	struct message_queue * queue = ctx->queue = skynet_mq_create(ctx->handle);
	// init function maybe use ctx->handle, so it must init at last
	_context_inc();

	CHECKCALLING_BEGIN(ctx)
	int r = skynet_module_instance_init(mod, inst, ctx, param);
	CHECKCALLING_END(ctx)
	if (r == 0) {
		struct skynet_context * ret = skynet_context_release(ctx);
		if (ret) {
			ctx->init = true;
		}
		skynet_mq_force_push(queue);
		if (ret) {
			printf("[:%x] launch %s %s\n",ret->handle, name, param ? param : "");
		}
		return ret;
	} else {
		skynet_context_release(ctx);
		skynet_handle_retire(ctx->handle);
		return NULL;
	}
}

int
skynet_context_newsession(struct skynet_context *ctx) {
	int session = ++ctx->session_id;
	return session;
}

void 
skynet_context_grab(struct skynet_context *ctx) {
	__sync_add_and_fetch(&ctx->ref,1);
}

static void 
_delete_context(struct skynet_context *ctx) {
	skynet_module_instance_release(ctx->mod, ctx->instance);
	skynet_mq_mark_release(ctx->queue);
	free(ctx);
	_context_dec();
}

struct skynet_context * 
skynet_context_release(struct skynet_context *ctx) {
	if (__sync_sub_and_fetch(&ctx->ref,1) == 0) {
		_delete_context(ctx);
		return NULL;
	}
	return ctx;
}

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
	int ret = skynet_harbor_message_isremote(handle);
	if (harbor) {
		*harbor = (int)(handle >> HANDLE_REMOTE_SHIFT);
	}
	return ret;
}

static void
_send_message(uint32_t des, struct skynet_message *msg) {
	if (skynet_harbor_message_isremote(des)) {
			struct remote_message * rmsg = malloc(sizeof(*rmsg));
			rmsg->destination.handle = des;
			rmsg->message = msg->data;
			rmsg->sz = msg->sz;
			skynet_harbor_send(rmsg, msg->source, msg->session);
	} else {
		if (skynet_context_push(des, msg)) {
			free(msg->data);
			skynet_error(NULL, "Drop message from %x forward to %x (size=%d)", msg->source, des, (int)msg->sz);
		}
	}
}

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
	int type = msg->sz >> HANDLE_REMOTE_SHIFT;
	size_t sz = msg->sz & HANDLE_MASK;
	if (type == PTYPE_MULTICAST) {
		skynet_multicast_dispatch((struct skynet_multicast_message *)msg->data, ctx, _mc);
	} else {
		int reserve = ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz);
		reserve |= _forwarding(ctx, msg);
		if (!reserve) {
			free(msg->data);
		}
	}
	CHECKCALLING_END(ctx)
}

int
skynet_context_message_dispatch(struct skynet_monitor *sm) {
	struct message_queue * q = skynet_globalmq_pop();
	if (q==NULL)
		return 1;

	uint32_t handle = skynet_mq_handle(q);

	struct skynet_context * ctx = skynet_handle_grab(handle);
	if (ctx == NULL) {
		int s = skynet_mq_release(q);
		if (s>0) {
			skynet_error(NULL, "Drop message queue %x (%d messages)", handle,s);
		}
		return 0;
	}

	struct skynet_message msg;
	if (skynet_mq_pop(q,&msg)) {
		skynet_context_release(ctx);
		return 0;
	}

	skynet_monitor_trigger(sm, msg.source , handle);

	if (ctx->cb == NULL) {
		free(msg.data);
		skynet_error(NULL, "Drop message from %x to %x without callback , size = %d",msg.source, handle, (int)msg.sz);
	} else {
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

const char * 
skynet_command(struct skynet_context * context, const char * cmd , const char * param) {
	if (strcmp(cmd,"TIMEOUT") == 0) {
		char * session_ptr = NULL;
		int ti = strtol(param, &session_ptr, 10);
		int session = skynet_context_newsession(context);
		if (session < 0) 
			return NULL;
		skynet_timeout(context->handle, ti, session);
		sprintf(context->result, "%d", session);
		return context->result;
	}

	if (strcmp(cmd,"LOCK") == 0) {
		if (context->init == false) {
			return NULL;
		}
		int session = strtol(param, NULL, 10);
		assert(session);
		skynet_mq_lock(context->queue, session);
		return NULL;
	}

	if (strcmp(cmd,"REG") == 0) {
		if (param == NULL || param[0] == '\0') {
			sprintf(context->result, ":%x", context->handle);
			return context->result;
		} else if (param[0] == '.') {
			return skynet_handle_namehandle(context->handle, param + 1);
		} else {
			assert(context->handle!=0);
			struct remote_name *rname = malloc(sizeof(*rname));
			_copy_name(rname->name, param);
			rname->handle = context->handle;
			skynet_harbor_register(rname);
			return NULL;
		}
	}

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
		if (name[0] == '.') {
			return skynet_handle_namehandle(handle_id, name + 1);
		} else {
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
		skynet_handle_retire(context->handle);
		return NULL;
	}

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
			skynet_handle_retire(handle);
		}
		return NULL;
	}

	if (strcmp(cmd,"LAUNCH") == 0) {
		size_t sz = strlen(param);
		char tmp[sz+1];
		strcpy(tmp,param);
		char * args = tmp;
		char * mod = strsep(&args, " \t\r\n");
		args = strsep(&args, "\r\n");
		struct skynet_context * inst = skynet_context_new(mod,args);
		if (inst == NULL) {
			fprintf(stderr, "Launch %s %s failed\n",mod,args);
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

	if (strcmp(cmd,"STARTTIME") == 0) {
		uint32_t sec = skynet_gettime_fixsec();
		sprintf(context->result,"%u",sec);
		return context->result;
	}

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

	if (strcmp(cmd,"ENDLESS") == 0) {
		if (context->endless) {
			strcpy(context->result, "1");
			context->endless = false;
			return context->result;
		}
		return NULL;
	}

	if (strcmp(cmd,"ABORT") == 0) {
		skynet_handle_retireall();
		return NULL;
	}

	return NULL;
}

void 
skynet_forward(struct skynet_context * context, uint32_t destination) {
	assert(context->forward == 0);
	context->forward = destination;
}

static void
_filter_args(struct skynet_context * context, int type, int *session, void ** data, size_t * sz) {
	int dontcopy = type & PTYPE_TAG_DONTCOPY;
	int allocsession = type & PTYPE_TAG_ALLOCSESSION;
	type &= 0xff;

	if (allocsession) {
		assert(*session == 0);
		*session = skynet_context_newsession(context);
	}

	char * msg;
	if (dontcopy || *data == NULL) {
		msg = *data;
	} else {
		msg = malloc(*sz+1);
		memcpy(msg, *data, *sz);
		msg[*sz] = '\0';
	}
	*data = msg;

	assert((*sz & HANDLE_MASK) == *sz);
	*sz |= type << HANDLE_REMOTE_SHIFT;
}

int
skynet_send(struct skynet_context * context, uint32_t source, uint32_t destination , int type, int session, void * data, size_t sz) {
	_filter_args(context, type, &session, (void **)&data, &sz);

	if (source == 0) {
		source = context->handle;
	}

	if (destination == 0) {
		return session;
	}
	if (skynet_harbor_message_isremote(destination)) {
		struct remote_message * rmsg = malloc(sizeof(*rmsg));
		rmsg->destination.handle = destination;
		rmsg->message = data;
		rmsg->sz = sz;
		skynet_harbor_send(rmsg, source, session);
	} else {
		struct skynet_message smsg;
		smsg.source = source;
		smsg.session = session;
		smsg.data = data;
		smsg.sz = sz;

		if (skynet_context_push(destination, &smsg)) {
			free(data);
			skynet_error(NULL, "Drop message from %x to %x (type=%d)(size=%d)", source, destination, type, (int)(sz & HANDLE_MASK));
			return -1;
		}
	}
	return session;
}

int
skynet_sendname(struct skynet_context * context, const char * addr , int type, int session, void * data, size_t sz) {
	uint32_t source = context->handle;
	uint32_t des = 0;
	if (addr[0] == ':') {
		des = strtoul(addr+1, NULL, 16);
	} else if (addr[0] == '.') {
		des = skynet_handle_findname(addr + 1);
		if (des == 0) {
			free(data);
			skynet_error(context, "Drop message to %s", addr);
			return session;
		}
	} else {
		_filter_args(context, type, &session, (void **)&data, &sz);

		struct remote_message * rmsg = malloc(sizeof(*rmsg));
		_copy_name(rmsg->destination.name, addr);
		rmsg->destination.handle = 0;
		rmsg->message = data;
		rmsg->sz = sz;

		skynet_harbor_send(rmsg, source, session);
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

void
skynet_context_send(struct skynet_context * ctx, void * msg, size_t sz, uint32_t source, int type, int session) {
	struct skynet_message smsg;
	smsg.source = source;
	smsg.session = session;
	smsg.data = msg;
	smsg.sz = sz | type << HANDLE_REMOTE_SHIFT;

	skynet_mq_push(ctx->queue, &smsg);
}
