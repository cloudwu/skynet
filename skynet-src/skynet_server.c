#include "skynet_server.h"
#include "skynet_module.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
#include "skynet_timer.h"
#include "skynet_harbor.h"
#include "skynet_env.h"
#include "skynet.h"

#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define BLACKHOLE "blackhole"
#define DEFAULT_MESSAGE_QUEUE 16 

struct skynet_context {
	void * instance;
	struct skynet_module * mod;
	uint32_t handle;
	int ref;
	char handle_name[10];
	char result[32];
	void * cb_ud;
	skynet_cb cb;
	int session_id;
	int in_global_queue;
	int init;
	uint32_t forward;
	char * forward_address;
	struct message_queue *queue;
};

static void
_id_to_hex(char * str, uint32_t id) {
	int i;
	static char hex[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
	for (i=0;i<8;i++) {
		str[i] = hex[(id >> ((7-i) * 4))&0xf];
	}
	str[8] = '\0';
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
	ctx->mod = mod;
	ctx->instance = inst;
	ctx->ref = 2;
	ctx->cb = NULL;
	ctx->cb_ud = NULL;
	// a trick, global loop can't be dispatch in init process
	ctx->in_global_queue = 1;

	ctx->forward = 0;
	ctx->forward_address = NULL;
	ctx->session_id = 0;
	ctx->init = 0;
	ctx->handle = skynet_handle_register(ctx);
	char * uid = ctx->handle_name;
	uid[0] = ':';
	_id_to_hex(uid+1, ctx->handle);
	struct message_queue * queue = ctx->queue = skynet_mq_create(ctx->handle);
	// init function maybe use ctx->handle, so it must init at last

	int r = skynet_module_instance_init(mod, inst, ctx, param);
	if (r == 0) {
		struct skynet_context * ret = skynet_context_release(ctx);
		if (ret) {
			ctx->init = 1;
			skynet_globalmq_push(ctx->queue);
			return ret;
		} else {
			// because of ctx->in_global_queue == 1 , so we should release queue here.
			skynet_mq_release(queue);
		}
		return NULL;
	} else {
		ctx->in_global_queue = 0;
		skynet_context_release(ctx);
		skynet_handle_retire(ctx->handle);
		return NULL;
	}
}

int
skynet_context_newsession(struct skynet_context *ctx) {
	int session = ++ctx->session_id;
	if (session >= 0x7fffffff) {
		ctx->session_id = 1;
		return 1;
	}

	return session;
}

void 
skynet_context_grab(struct skynet_context *ctx) {
	__sync_add_and_fetch(&ctx->ref,1);
}

static void 
_delete_context(struct skynet_context *ctx) {
	skynet_module_instance_release(ctx->mod, ctx->instance);
	if (!ctx->in_global_queue) {
		skynet_mq_release(ctx->queue);
	}
	free(ctx);
}

struct skynet_context * 
skynet_context_release(struct skynet_context *ctx) {
	if (__sync_sub_and_fetch(&ctx->ref,1) == 0) {
		_delete_context(ctx);
		return NULL;
	}
	return ctx;
}

static int
_forwarding(struct skynet_context *ctx, struct skynet_message *msg) {
	if (ctx->forward) {
		uint32_t des = ctx->forward;
		ctx->forward = 0;
		if (skynet_harbor_message_isremote(des)) {
			skynet_harbor_send(NULL, des, msg);
		} else {
			if (skynet_context_push(des, msg)) {
				free(msg->data);
				skynet_error(NULL, "Drop message from %x forward to %x (size=%d)", msg->source, des, (int)msg->sz);
				return 1;
			}
		}
		return 1;
	}
	if (ctx->forward_address) {
		skynet_harbor_send(ctx->forward_address, 0, msg);
		free(ctx->forward_address);
		ctx->forward_address = NULL;
		return 1;
	}
	return 0;
}

static void
_dispatch_message(struct skynet_context *ctx, struct skynet_message *msg) {
	assert(ctx->init);
	if (msg->source == SKYNET_SYSTEM_TIMER) {
		ctx->cb(ctx, ctx->cb_ud, msg->session, NULL, msg->data, msg->sz);
	} else {
		char tmp[10];
		tmp[0] = ':';
		int not_delete;
		_id_to_hex(tmp+1, msg->source);
		if (skynet_harbor_message_isremote(msg->source)) {
			void * data = skynet_harbor_message_open(msg);
			ctx->cb(ctx, ctx->cb_ud, msg->session, tmp, data, msg->sz);
			not_delete = _forwarding(ctx, msg);
			if (!not_delete) {
				skynet_harbor_message_close(msg);
			}
		} else {
			ctx->cb(ctx, ctx->cb_ud, msg->session, tmp, msg->data, msg->sz);
			not_delete = _forwarding(ctx, msg);
		}
		if (!not_delete) {
			free(msg->data);
		}
	}
}

static int
_drop_queue(struct message_queue *q) {
	// todo: send message back to message source
	struct skynet_message msg;
	int s = 0;
	while(!skynet_mq_pop(q, &msg)) {
		++s;
		if (skynet_harbor_message_isremote(msg.source)) {
			skynet_harbor_message_close(&msg);
		}
		free(msg.data);
	}
	skynet_mq_release(q);
	return s;
}

int
skynet_context_message_dispatch(void) {
	struct message_queue * q = skynet_globalmq_pop();
	if (q==NULL)
		return 1;

	uint32_t handle = skynet_mq_handle(q);

	struct skynet_context * ctx = skynet_handle_grab(handle);
	if (ctx == NULL) {
		int s = _drop_queue(q);
		if (s>0) {
			skynet_error(NULL, "Drop message queue %x (%d messages)", handle,s);
		}
		return 0;
	}

	assert(ctx->in_global_queue);

	struct skynet_message msg;
	if (skynet_mq_pop(q,&msg)) {
		__sync_lock_release(&ctx->in_global_queue);
		skynet_context_release(ctx);
		return 0;
	}

	if (ctx->cb == NULL) {
		if (skynet_harbor_message_isremote(msg.source)) {
			skynet_harbor_message_close(&msg);
		}
		free(msg.data);
		skynet_error(NULL, "Drop message from %x to %x without callback , size = %d",msg.source, handle, (int)msg.sz);
	} else {
		_dispatch_message(ctx, &msg);
	}

	assert(q == ctx->queue);
	skynet_globalmq_push(q);
	skynet_context_release(ctx);

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

	if (strcmp(cmd,"NOW") == 0) {
		uint32_t ti = skynet_gettime();
		sprintf(context->result,"%u",ti);
		return context->result;
	}

	if (strcmp(cmd,"REG") == 0) {
		if (param == NULL || param[0] == '\0') {
			return context->handle_name;
		} else if (param[0] == '.') {
			return skynet_handle_namehandle(context->handle, param + 1);
		} else {
			assert(context->handle!=0);
			skynet_harbor_register(param, context->handle);
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
			skynet_harbor_register(name, handle_id);
		}
		return NULL;
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
			context->result[0] = ':';
			_id_to_hex(context->result+1, inst->handle);
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

	return NULL;
}

void 
skynet_forward(struct skynet_context * context, const char * addr) {
	uint32_t des = 0;
	assert(context->forward == 0 && context->forward_address == NULL);
	if (addr[0] == ':') {
		des = strtol(addr+1, NULL, 16);
		assert(des != 0);
	} else if (addr[0] == '.') {
		des = skynet_handle_findname(addr + 1);
		if (des == 0) {
			skynet_error(context, "Drop message forward %s", addr);
			return;
		}
	} else {
		context->forward_address = strdup(addr);
		return;
	}
	context->forward = des;
}

int
skynet_send(struct skynet_context * context, const char * source, const char * addr , int session, void * data, size_t sz, int flags) {
	uint32_t source_handle;
	if (source == NULL) {
		source_handle = context->handle;
	} else {
		assert (source[0] == ':');
		source_handle = strtoul(source+1, NULL, 16);
	}

	char * msg;
	if ((flags & DONTCOPY) || data == NULL) {
		msg = data;
	} else {
		msg = malloc(sz+1);
		memcpy(msg, data, sz);
		msg[sz] = '\0';
	}
	int session_id = session;
	if (session < 0) {
		session = skynet_context_newsession(context);
		session_id = - session;
	}
	uint32_t des = 0;
	if (addr[0] == ':') {
		des = strtoul(addr+1, NULL, 16);
	} else if (addr[0] == '.') {
		des = skynet_handle_findname(addr + 1);
		if (des == 0) {
			free(msg);
			skynet_error(context, "Drop message to %s, size = %d", addr, (int)sz);
			return session;
		}
	} else {
		struct skynet_message smsg;
		smsg.source = source_handle;
		smsg.session = session_id;
		smsg.data = msg;
		smsg.sz = sz;
		skynet_harbor_send(addr, 0, &smsg);
		return session;
	}

	assert(des > 0);

	struct skynet_message smsg;
	smsg.source = source_handle;
	smsg.session = session_id;
	smsg.data = msg;
	smsg.sz = sz;

	if (skynet_harbor_message_isremote(des)) {
		skynet_harbor_send(NULL, des, &smsg);
	} else if (skynet_context_push(des, &smsg)) {
		free(msg);
		skynet_error(NULL, "Drop message from %x to %s (size=%d)", smsg.source, addr, (int)sz);
		return -1;
	}
	return session;
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
	assert(context->cb == NULL);
	context->cb = cb;
	context->cb_ud = ud;
}

int
skynet_context_push(uint32_t handle, struct skynet_message *message) {
	struct skynet_context * ctx = skynet_handle_grab(handle);
	if (ctx == NULL) {
		return -1;
	}
	skynet_mq_push(ctx->queue, message);
	if (__sync_lock_test_and_set(&ctx->in_global_queue, 1) == 0)  {
		skynet_globalmq_push(ctx->queue);
	}
	skynet_context_release(ctx);

	return 0;
}
