#include "skynet_server.h"
#include "skynet_imp.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_module.h"
#include "skynet_timer.h"
#include "skynet_harbor.h"
#include "skynet_group.h"

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>

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
	pthread_t pid[thread+1];

	pthread_create(&pid[0], NULL, _timer, NULL);

	int i;

	for (i=1;i<thread+1;i++) {
		pthread_create(&pid[i], NULL, _worker, NULL);
	}

	for (i=0;i<thread+1;i++) {
		pthread_join(pid[i], NULL); 
	}
}

static int
_start_master(const char * master) {
	struct skynet_context *ctx = skynet_context_new("master", master);
	if (ctx == NULL)
		return 1;
	return 0;	
}

void 
skynet_start(struct skynet_config * config) {
	skynet_group_init();
	skynet_harbor_init(config->harbor);
	skynet_handle_init(config->harbor);
	skynet_mq_init(config->mqueue_size);
	skynet_module_init(config->module_path);
	skynet_timer_init();

	if (config->standalone) {
		if (_start_master(config->standalone)) {
			return;
		}
	}
	// harbor must be init first
	if (skynet_harbor_start(config->master , config->local)) {
		fprintf(stderr, "Init fail : no master");
		return;
	}

	struct skynet_context *ctx;
	ctx = skynet_context_new("logger", config->logger);
	if (ctx == NULL) {
		return;
	}
	ctx = skynet_context_new("snlua", "launcher");
	assert(ctx);
	ctx = skynet_context_new("snlua", config->start);

	_start(config->thread);
}

