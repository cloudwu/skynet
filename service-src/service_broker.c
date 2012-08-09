#include "skynet.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#define DEFAULT_NUMBER 16
#define LAUNCHER ".launcher"

struct worker {
	int init;
	char * name;
};

struct broker {
	int init;
	int id;
	char * name;
	struct worker w[DEFAULT_NUMBER];
};

struct broker * 
broker_create(void) {
	struct broker *b = malloc(sizeof(*b));
	memset(b,0,sizeof(*b));
	return b;
}

void
broker_release(struct broker * b) {
	int i;
	for (i=0;i<DEFAULT_NUMBER;i++) {
		free(b->w[i].name);
	}
	free(b->name);
	free(b);
}

static void
_init(struct broker *b, int session, const char * msg, size_t sz) {
	assert(session > 0 && session <= DEFAULT_NUMBER);
	assert(msg);
	int id = session - 1;
	assert(b->w[id].init == 0);
	b->w[id].name = malloc(sz+1);
	memcpy(b->w[id].name, msg, sz);
	b->w[id].name[sz] = '\0';
	b->w[id].init = 1;
	++b->init;
}

static void
_forward(struct broker *b, struct skynet_context * context) {
	skynet_forward(context, b->w[b->id].name);
	b->id = (b->id + 1) % DEFAULT_NUMBER;
}

static void
_cb(struct skynet_context * context, void * ud, int session, const char * addr, const void * msg, size_t sz) {
	struct broker * b = ud;
	if (b->init < DEFAULT_NUMBER) {
		_init(b, session, msg, sz);
		if (b->init == DEFAULT_NUMBER) {
			skynet_command(context, "REG", b->name);
			skynet_send(context, LAUNCHER, 0, NULL, 0, 0);
		}
	} else {
		_forward(b, context);
	}
}


int
broker_init(struct broker *b, struct skynet_context *ctx, const char * args) {
	char * service = strchr(args,' ');
	if (service == NULL) {
		return 1;
	}
	int len = service - args;
	if (len>0) {
		b->name = malloc(len +1);
		memcpy(b->name, args, len);
		b->name[len] = '\0';
	}
	service++;

	int i;
	len = strlen(service);
	if (len == 0)
		return 1;
	for (i=0;i<DEFAULT_NUMBER;i++) {
		int id = skynet_send(ctx, LAUNCHER , -1, service , len, 0);
		assert(id > 0 && id <= DEFAULT_NUMBER);
	}

	skynet_callback(ctx, b, _cb);

	return 0;
}
