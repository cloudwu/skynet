#include "skynet_server.h"
#include "skynet_module.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
#include "skynet_timer.h"
#include "skynet_harbor.h"
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
	struct message_queue *queue;
};

static void
_id_to_hex(char * str, int id) {
	int i;
	static char hex[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
	for (i=0;i<8;i++) {
		str[i] = hex[(id >> ((7-i) * 4))&0xf];
	}
	str[8] = '\0';
}

struct skynet_context * 
skynet_context_new(const char * name, const char *parm) {
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
	ctx->in_global_queue = 0;
	ctx->session_id = 0;
	char * uid = ctx->handle_name;
	uid[0] = ':';
	_id_to_hex(uid+1, ctx->handle);

	ctx->handle = skynet_handle_register(ctx);
	ctx->queue = skynet_mq_create(ctx->handle);
	// init function maybe use ctx->handle, so it must init at last

	int r = skynet_module_instance_init(mod, inst, ctx, parm);
	if (r == 0) {
		return skynet_context_release(ctx);
	} else {
		skynet_context_release(ctx);
		skynet_handle_retire(ctx->handle);
		return NULL;
	}
}

static int
_new_session(struct skynet_context *ctx) {
	int session = ++ctx->session_id;
	if (session < 0) {
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

static void
_dispatch_message(struct skynet_context *ctx, struct skynet_message *msg) {
	if (msg->source == SKYNET_SYSTEM_TIMER) {
		ctx->cb(ctx, ctx->cb_ud, msg->session, NULL, msg->data, msg->sz);
	} else {
		char tmp[10];
		tmp[0] = ':';
		_id_to_hex(tmp+1, msg->source);
		if (skynet_harbor_message_isremote(msg->source)) {
			void * data = skynet_harbor_message_open(msg);
			ctx->cb(ctx, ctx->cb_ud, msg->session, tmp, data, msg->sz);
			skynet_harbor_message_close(msg);
		} else {
			ctx->cb(ctx, ctx->cb_ud, msg->session, tmp, msg->data, msg->sz);
		}

		free(msg->data);
	}
}

static void
_drop_queue(struct message_queue *q) {
	// todo: send message back to message source
	struct skynet_message msg;
	while(!skynet_mq_pop(q, &msg)) {
		if (skynet_harbor_message_isremote(msg.source)) {
			skynet_harbor_message_close(&msg);
		}
		free(msg.data);
	}
	skynet_mq_release(q);
}

int
skynet_context_message_dispatch(void) {
	struct message_queue * q = skynet_globalmq_pop();
	if (q==NULL)
		return 1;

	uint32_t handle = skynet_mq_handle(q);

	struct skynet_context * ctx = skynet_handle_grab(handle);
	if (ctx == NULL) {
		skynet_error(NULL, "Drop message queue %u ", handle);
		_drop_queue(q);
		return 0;
	}

	assert(ctx->in_global_queue);

	struct skynet_message msg;
	if (skynet_mq_pop(q,&msg)) {
		// empty queue
		__sync_lock_release(&ctx->in_global_queue);
		skynet_context_release(ctx);
		return 0;
	}

	if (ctx->cb == NULL) {
		if (skynet_harbor_message_isremote(msg.source)) {
			skynet_harbor_message_close(&msg);
		}
		free(msg.data);
		skynet_error(NULL, "Drop message from %u to %u without callback , size = %d",msg.source, handle, (int)msg.sz);
	} else {
		_dispatch_message(ctx, &msg);
	}

	skynet_context_release(ctx);

	skynet_globalmq_push(q);

	return 0;
}

const char * 
skynet_command(struct skynet_context * context, const char * cmd , int session, const char * parm) {
	if (strcmp(cmd,"TIMEOUT") == 0) {
		char * session_ptr = NULL;
		int ti = strtol(parm, &session_ptr, 10);
		skynet_timeout(context->handle, ti, session);
		return NULL;
	}

	if (strcmp(cmd,"NOW") == 0) {
		uint32_t ti = skynet_gettime();
		sprintf(context->result,"%u",ti);
		return context->result;
	}

	if (strcmp(cmd,"REG") == 0) {
		if (parm == NULL || parm[0] == '\0') {
			return context->handle_name;
		} else if (parm[0] == '.') {
			return skynet_handle_namehandle(context->handle, parm + 1);
		} else {
			assert(context->handle!=0);
			skynet_harbor_register(parm, context->handle);
			return NULL;
		}
	}

	if (strcmp(cmd,"EXIT") == 0) {
		skynet_handle_retire(context->handle);
		return NULL;
	}

	if (strcmp(cmd,"LAUNCH") == 0) {
		size_t sz = strlen(parm);
		char tmp[sz+1];
		strcpy(tmp,parm);
		char * parm = tmp;
		char * mod = strsep(&parm, " \t\r\n");
		parm = strsep(&parm, "\r\n");
		struct skynet_context * inst = skynet_context_new(mod,parm);
		if (inst == NULL) {
			return NULL;
		} else {
			context->result[0] = ':';
			_id_to_hex(context->result+1, inst->handle);
			return context->result;
		}
	}

	return NULL;
}

void 
skynet_send(struct skynet_context * context, const char * addr , int session, void * msg, size_t sz) {
	if (session < 0) {
		session = _new_session(context);
	}
	uint32_t des = 0;
	if (addr[0] == ':') {
		des = strtol(addr+1, NULL, 16);
	} else if (addr[0] == '.') {
		des = skynet_handle_findname(addr + 1);
		if (des == 0) {
			free(msg);
			skynet_error(context, "Drop message to %s, size = %d", addr, (int)sz);
			return;
		}
	} else {
		struct skynet_message smsg;
		smsg.source = context->handle;
		smsg.session = session;
		smsg.data = msg;
		smsg.sz = sz;
		skynet_harbor_send(addr, 0, &smsg);
		return;
	}

	assert(des > 0);

	struct skynet_message smsg;
	smsg.source = context->handle;
	smsg.session = session;
	smsg.data = msg;
	smsg.sz = sz;

	if (skynet_harbor_message_isremote(des)) {
		skynet_harbor_send(NULL, des, &smsg);
	} else if (skynet_context_push(des, &smsg)) {
		free(msg);
		skynet_error(NULL, "Drop message from %u to %s (size=%d)", smsg.source, addr, (int)sz);
		return;
	}
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
	if (message->session < 0) {
		message->session = _new_session(ctx);
	}
	skynet_mq_push(ctx->queue, message);
	if (__sync_lock_test_and_set(&ctx->in_global_queue,1) == 0) {
		skynet_globalmq_push(ctx->queue);
	}
	skynet_context_release(ctx);

	return 0;
}
