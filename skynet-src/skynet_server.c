#include "skynet.h"

#include "skynet_server.h"
#include "skynet_module.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
#include "skynet_timer.h"
#include "skynet_harbor.h"
#include "skynet_env.h"
#include "skynet_monitor.h"
#include "skynet_imp.h"

#include <pthread.h>

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
	struct message_queue *queue;
	bool init;
	bool endless;

	CHECKCALLING_DECL
};

struct skynet_node {
	int total;
	uint32_t monitor_exit;
	pthread_key_t handle_key;
};

static struct skynet_node G_NODE;

int 
skynet_context_total() {
	return G_NODE.total;
}

static void
_context_inc() {
	__sync_fetch_and_add(&G_NODE.total,1);
}

static void
_context_dec() {
	__sync_fetch_and_sub(&G_NODE.total,1);
}

uint32_t 
skynet_current_handle(void) {
	void * handle = pthread_getspecific(G_NODE.handle_key);
	return (uint32_t)(uintptr_t)handle;
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

struct drop_t {
	uint32_t handle;
};

static void
drop_message(struct skynet_message *msg, void *ud) {
	struct drop_t *d = ud;
	skynet_free(msg->data);
	uint32_t source = d->handle;
	assert(source);
	// report error to the message source
	skynet_send(NULL, source, msg->source, PTYPE_ERROR, 0, NULL, 0);
}

struct skynet_context * 
skynet_context_new(const char * name, const char *param) {
	struct skynet_module * mod = skynet_module_query(name);

	if (mod == NULL)
		return NULL;

	void *inst = skynet_module_instance_create(mod);
	if (inst == NULL)
		return NULL;
	struct skynet_context * ctx = skynet_malloc(sizeof(*ctx));
	CHECKCALLING_INIT(ctx)

	ctx->mod = mod;
	ctx->instance = inst;
	ctx->ref = 2;
	ctx->cb = NULL;
	ctx->cb_ud = NULL;
	ctx->session_id = 0;

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
			skynet_error(ret, "LAUNCH %s %s", name, param ? param : "");
		}
		return ret;
	} else {
		skynet_error(ctx, "FAILED launch %s", name);
		uint32_t handle = ctx->handle;
		skynet_context_release(ctx);
		skynet_handle_retire(handle);
		struct drop_t d = { handle };
		skynet_mq_release(queue, drop_message, &d);
		return NULL;
	}
}

int
skynet_context_newsession(struct skynet_context *ctx) {
	// session always be a positive number
	int session = (++ctx->session_id) & 0x7fffffff;
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
	skynet_free(ctx);
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
_dispatch_message(struct skynet_context *ctx, struct skynet_message *msg) {
	assert(ctx->init);
	CHECKCALLING_BEGIN(ctx)
	pthread_setspecific(G_NODE.handle_key, (void *)(uintptr_t)(ctx->handle));
	int type = msg->sz >> HANDLE_REMOTE_SHIFT;
	size_t sz = msg->sz & HANDLE_MASK;
	if (!ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz)) {
		skynet_free(msg->data);
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
		struct drop_t d = { handle };
		skynet_mq_release(q, drop_message, &d);
		return 0;
	}

	struct skynet_message msg;
	if (skynet_mq_pop(q,&msg)) {
		skynet_context_release(ctx);
		return 0;
	}

	skynet_monitor_trigger(sm, msg.source , handle);

	if (ctx->cb == NULL) {
		skynet_free(msg.data);
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
	} else {
		skynet_error(context, "KILL :%0x", handle);
	}
	if (G_NODE.monitor_exit) {
		skynet_send(context,  handle, G_NODE.monitor_exit, PTYPE_CLIENT, 0, NULL, 0);
	}
	skynet_handle_retire(handle);
}

////////////////////////////////////////////////////////////////////////////////

static const char *
_cmd_timeout(struct skynet_context *context, const char *cmd , const char *param) {
	char *session_ptr = NULL;
	int ti = strtol(param, &session_ptr, 10);
	int session = skynet_context_newsession(context);
	skynet_timeout(context->handle, ti, session);
	sprintf(context->result, "%d", session);
	return context->result;
}

static const char *
_cmd_reg(struct skynet_context *context, const char *cmd , const char *param) {
	if (param == NULL || param[0] == '\0') {
		sprintf(context->result, ":%x", context->handle);
		return context->result;
	} else if (param[0] == '.') {
		return skynet_handle_namehandle(context->handle, param + 1);
	} else {
		assert(context->handle != 0);
		struct remote_name *rname = skynet_malloc(sizeof(*rname));
		_copy_name(rname->name, param);
		rname->handle = context->handle;
		skynet_harbor_register(rname);
		return NULL;
	}
}

static const char *
_cmd_query(struct skynet_context *context, const char *cmd , const char *param) {
	if (param[0] == '.') {
		uint32_t handle = skynet_handle_findname(param + 1);
		sprintf(context->result, ":%x", handle);
		return context->result;
	}
	return NULL;
}

static const char *
_cmd_name(struct skynet_context *context, const char *cmd , const char *param) {
	int size = strlen(param);
	char name[size + 1];
	char handle[size + 1];
	sscanf(param, "%s %s", name, handle);
	if (handle[0] != ':') {
		return NULL;
	}
	uint32_t handle_id = strtoul(handle + 1, NULL, 16);
	if (handle_id == 0) {
		return NULL;
	}
	if (name[0] == '.') {
		return skynet_handle_namehandle(handle_id, name + 1);
	} else {
		struct remote_name *rname = skynet_malloc(sizeof(*rname));
		_copy_name(rname->name, name);
		rname->handle = handle_id;
		skynet_harbor_register(rname);
	}
	return NULL;
}

static const char *
_cmd_now(struct skynet_context *context, const char *cmd , const char *param) {
	uint32_t ti = skynet_gettime();
	sprintf(context->result, "%u", ti);
	return context->result;
}

static const char *
_cmd_exit(struct skynet_context *context, const char *cmd , const char *param) {
	handle_exit(context, 0);
	return NULL;
}

static const char *
_cmd_kill(struct skynet_context *context, const char *cmd , const char *param) {
	uint32_t handle = 0;
	if (param[0] == ':') {
		handle = strtoul(param + 1, NULL, 16);
	} else if (param[0] == '.') {
		handle = skynet_handle_findname(param + 1);
	} else {
		skynet_error(context, "Can't kill %s", param);
		// todo : kill global service
	}
	if (handle) {
		handle_exit(context, handle);
	}
	return NULL;
}

static const char *
_cmd_launch(struct skynet_context *context, const char *cmd , const char *param) {
	size_t sz = strlen(param);
	char tmp[sz + 1];
	strcpy(tmp, param);
	char *args = tmp;
	char *mod = strsep(&args, " \t\r\n");
	args = strsep(&args, "\r\n");
	struct skynet_context *inst = skynet_context_new(mod, args);
	if (inst == NULL) {
		return NULL;
	} else {
		_id_to_hex(context->result, inst->handle);
		return context->result;
	}
}

static const char *
_cmd_getenv(struct skynet_context *context, const char *cmd , const char *param) {
	return skynet_getenv(param);
}

static const char *
_cmd_setenv(struct skynet_context *context, const char *cmd , const char *param) {
	size_t sz = strlen(param);
	char key[sz + 1];
	int i;
	for (i = 0; param[i] != ' ' && param[i]; i++) {
		key[i] = param[i];
	}
	if (param[i] == '\0') {
		return NULL;
	}

	key[i] = '\0';
	param += i + 1;

	skynet_setenv(key, param);
	return NULL;
}

static const char *
_cmd_starttime(struct skynet_context *context, const char *cmd , const char *param) {
	uint32_t sec = skynet_gettime_fixsec();
	sprintf(context->result, "%u", sec);
	return context->result;
}

static const char *
_cmd_endless(struct skynet_context *context, const char *cmd , const char *param) {
	if (context->endless) {
		strcpy(context->result, "1");
		context->endless = false;
		return context->result;
	}
	return NULL;
}

static const char *
_cmd_abort(struct skynet_context *context, const char *cmd , const char *param) {
	skynet_handle_retireall();
	return NULL;
}

static const char *
_cmd_monitor(struct skynet_context *context, const char *cmd , const char *param) {
	uint32_t handle = 0;
	if (param == NULL || param[0] == '\0') {
		if (G_NODE.monitor_exit) {
			// return current monitor serivce
			sprintf(context->result, ":%x", G_NODE.monitor_exit);
			return context->result;
		}
		return NULL;
	} else {
		if (param[0] == ':') {
			handle = strtoul(param + 1, NULL, 16);
		} else if (param[0] == '.') {
			handle = skynet_handle_findname(param + 1);
		} else {
			skynet_error(context, "Can't monitor %s", param);
			// todo : monitor global service
		}
	}
	G_NODE.monitor_exit = handle;
	return NULL;
}

static const char *
_cmd_mqlen(struct skynet_context *context, const char *cmd , const char *param) {
	int len = skynet_mq_length(context->queue);
	sprintf(context->result, "%d", len);
	return context->result;
}

struct command_reg {
	const char *name;
	const char *(*command)(struct skynet_context *context, const char *cmd , const char *param);
};

static struct command_reg cmd_reg[] = {
	{ "TIMEOUT"  , _cmd_timeout   },
	{ "REG"      , _cmd_reg       },
	{ "QUERY"    , _cmd_query     },
	{ "NAME"     , _cmd_name      },
	{ "NOW"      , _cmd_now       },
	{ "EXIT"     , _cmd_exit      },
	{ "KILL"     , _cmd_kill      },
	{ "LAUNCH"   , _cmd_launch    },
	{ "GETENV"   , _cmd_getenv    },
	{ "SETENV"   , _cmd_setenv    },
	{ "STARTTIME", _cmd_starttime },
	{ "ENDLESS"  , _cmd_endless   },
	{ "ABORT"    , _cmd_abort     },
	{ "MONITOR"  , _cmd_monitor   },
	{ "MQLEN"    , _cmd_mqlen     },
	{ NULL, NULL },
};

const char *
skynet_command(struct skynet_context *context, const char *cmd , const char *param) {
	struct command_reg *r;
	for (r = cmd_reg; r->name != NULL; ++r) {
		if (strcmp(cmd, r->name) == 0) {
			return r->command(context, cmd, param);
		}
	}
	return NULL;
}

static void
_filter_args(struct skynet_context * context, int type, int *session, void ** data, size_t * sz) {
	int needcopy = !(type & PTYPE_TAG_DONTCOPY);
	int allocsession = type & PTYPE_TAG_ALLOCSESSION;
	type &= 0xff;

	if (allocsession) {
		assert(*session == 0);
		*session = skynet_context_newsession(context);
	}

	if (needcopy && *data) {
		char * msg = skynet_malloc(*sz+1);
		memcpy(msg, *data, *sz);
		msg[*sz] = '\0';
		*data = msg;
	}

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
		struct remote_message * rmsg = skynet_malloc(sizeof(*rmsg));
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
			skynet_free(data);
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
			if (type & PTYPE_TAG_DONTCOPY) {
				skynet_free(data);
			}
			return session;
		}
	} else {
		_filter_args(context, type, &session, (void **)&data, &sz);

		struct remote_message * rmsg = skynet_malloc(sizeof(*rmsg));
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

void 
skynet_globalinit(void) {
	G_NODE.total = 0;
	G_NODE.monitor_exit = 0;
	if (pthread_key_create(&G_NODE.handle_key, NULL)) {
		fprintf(stderr, "pthread_key_create failed");
		exit(1);
	}
	// set mainthread's key
	skynet_initthread(THREAD_MAIN);
}

void 
skynet_globalexit(void) {
	pthread_key_delete(G_NODE.handle_key);
}

void
skynet_initthread(int m) {
	uintptr_t v = (uint32_t)(-m);
	pthread_setspecific(G_NODE.handle_key, (void *)v);
}

