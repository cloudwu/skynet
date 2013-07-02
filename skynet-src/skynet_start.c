#include "skynet.h"
#include "skynet_server.h"
#include "skynet_imp.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_module.h"
#include "skynet_timer.h"
#include "skynet_harbor.h"
#include "skynet_group.h"
#include "skynet_monitor.h"

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct monitor {
	int count;
	struct skynet_monitor ** m;
};

#define CHECK_ABORT if (skynet_context_total()==0) break;

static void *
_monitor(void *p) {
	struct monitor * m = p;
	int i;
	int n = m->count;
	for (;;) {
		for (i=0;i<n;i++) {
			skynet_monitor_check(m->m[i]);
		}
		CHECK_ABORT
		sleep(5);
	}
	for (i=0;i<n;i++) {
		skynet_monitor_delete(m->m[i]);
	}
	free(m->m);
	free(m);

	return NULL;
}

static void *
_timer(void *p) {
	for (;;) {
		skynet_updatetime();
		CHECK_ABORT
		usleep(2500);
	}
	return NULL;
}

static void *
_worker(void *p) {
	struct skynet_monitor *sm = p;
	for (;;) {
		if (skynet_context_message_dispatch(sm)) {
			CHECK_ABORT
			usleep(1000);
		} 
	}
	return NULL;
}

static void
_start(int thread) {
	pthread_t pid[thread+2];

	struct monitor *m = malloc(sizeof(*m));
	m->count = thread;
	m->m = malloc(thread * sizeof(struct skynet_monitor *));
	int i;
	for (i=0;i<thread;i++) {
		m->m[i] = skynet_monitor_new();
	}

	pthread_create(&pid[0], NULL, _monitor, m);
	pthread_create(&pid[1], NULL, _timer, NULL);

	for (i=0;i<thread;i++) {
		pthread_create(&pid[i+2], NULL, _worker, m->m[i]);
	}

	for (i=1;i<thread+2;i++) {
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
	skynet_mq_init();
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
		fprintf(stderr,"launch logger error");
		exit(1);
	}
	ctx = skynet_context_new("localcast", NULL);
	if (ctx == NULL) {
		fprintf(stderr,"launch local cast error");
		exit(1);
	}
	ctx = skynet_context_new("snlua", "launcher");
	if (ctx) {
		skynet_command(ctx, "REG", ".launcher");
		ctx = skynet_context_new("snlua", config->start);
	}

	_start(config->thread);
}

