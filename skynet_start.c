#include "skynet_server.h"
#include "skynet_imp.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_module.h"
#include "skynet_timer.h"
#include "skynet_harbor.h"

#include <pthread.h>
#include <unistd.h>
#include <assert.h>

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

void 
skynet_start(struct skynet_config * config) {
	// harbor must be init first
	skynet_harbor_init(config->master , config->local, config->harbor);
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

