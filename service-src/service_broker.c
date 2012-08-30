#include "skynet.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#define DEFAULT_NUMBER 16
#define LAUNCHER ".launcher"

struct worker {
	int init;
	uint32_t address;
};

struct broker {
	int init;
	int id;
	uint32_t launcher;
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
	free(b->name);
	free(b);
}

static void
_init(struct broker *b, int session, uint32_t address) {
	assert(session > 0 && session <= DEFAULT_NUMBER);
	int id = session - 1;
	assert(b->w[id].init == 0);
	b->w[id].address = address;
	b->w[id].init = 1;
	++b->init;
}

static void
_forward(struct broker *b, struct skynet_context * context) {
	skynet_forward(context, b->w[b->id].address);
	b->id = (b->id + 1) % DEFAULT_NUMBER;
}

static int
_cb(struct skynet_context * context, void * ud, int session, uint32_t source, const void * msg, size_t sz) {
	struct broker * b = ud;
	if (b->init < DEFAULT_NUMBER) {
		if (source != b->launcher)
			return 0;
		assert(sz == 9);
		char addr[10];
		memcpy(addr, msg, 9);
		addr[9] = '\0';
		uint32_t address = strtoul(addr+1, NULL, 16);
		assert(address != 0);
		_init(b, session, address);
		if (b->init == DEFAULT_NUMBER) {
			skynet_command(context, "REG", b->name);
			skynet_send(context, 0, b->launcher, 0, NULL, 0, 0);
		}
	} else {
		_forward(b, context);
	}

	return 0;
}


int
broker_init(struct broker *b, struct skynet_context *ctx, const char * args) {
	b->launcher = skynet_queryname(ctx, LAUNCHER);
	if (b->launcher == 0) {
		skynet_error(ctx, "Can't query %s", LAUNCHER);
		return 1;
	}

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
		int id = skynet_send(ctx, 0, b->launcher , -1, service , len, 0);
		assert(id > 0 && id <= DEFAULT_NUMBER);
	}

	skynet_callback(ctx, b, _cb);

	return 0;
}
