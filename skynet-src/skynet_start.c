#include "skynet_server.h"
#include "skynet_imp.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_module.h"
#include "skynet_timer.h"
#include "skynet_harbor.h"
#include "skynet_master.h"

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <zmq.h>

static void *
_timer(void *p) {
	for (;;) {
		skynet_updatetime();
		usleep(2500);
	}
	return NULL;
}

static void *
_worker(void *p) {
	for (;;) {
		if (skynet_context_message_dispatch()) {
			usleep(1000);
		}
	}
	return NULL;
}

static void
_start(int thread) {
	pthread_t pid[thread+2];

	pthread_create(&pid[0], NULL, _timer, NULL);
	pthread_create(&pid[1], NULL, skynet_harbor_dispatch_thread, NULL);

	int i;
	for (i=2;i<thread+2;i++) {
		pthread_create(&pid[i], NULL, _worker, NULL);
	}

	for (i=0;i<thread+2;i++) {
		pthread_join(pid[i], NULL); 
	}
}

struct master_arg {
	void * context;
	const char * port;
};

static void *
_master_thread(void *ud) {
	struct master_arg * args = ud;
	skynet_master(args->context, args->port);
	return NULL;
}

static void
_start_master(void * context, const char * port) {
	pthread_t pid;
	struct master_arg args;
	args.context = context;
	args.port = port;
	pthread_create(&pid, NULL, _master_thread, &args);
}

void 
skynet_start(struct skynet_config * config) {
	void *context = zmq_init (1);
	assert(context);
	if (config->standalone) {
		_start_master(context, config->master);
	}
	// harbor must be init first
	skynet_harbor_init(context, config->master , config->local, config->harbor);
	skynet_handle_init(config->harbor);
	skynet_mq_init(config->mqueue_size);
	skynet_module_init(config->module_path);
	skynet_timer_init();
	struct skynet_context *ctx;
	ctx = skynet_context_new("logger", config->logger);
	assert(ctx);
	ctx = skynet_context_new("snlua", config->start);

	_start(config->thread);
}

