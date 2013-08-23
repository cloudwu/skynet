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
#include "skynet_socket.h"

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct monitor {
	int count;
	struct skynet_monitor ** m;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int sleep;
};

struct worker_parm {
	struct monitor *m;
	int id;
};

#define CHECK_ABORT if (skynet_context_total()==0) break;

static void
wakeup(struct monitor *m, int busy) {
	if (m->sleep >= m->count - busy) {
		pthread_mutex_lock(&m->mutex);
		pthread_cond_signal(&m->cond);
		pthread_mutex_unlock(&m->mutex);
	}
}

static void *
_socket(void *p) {
	struct monitor * m = p;
	for (;;) {
		int r = skynet_socket_poll();
		if (r==0)
			break;
		if (r<0)
			continue;
		// FIXME: wakeup will kill some performance when lots of connections
		wakeup(m,0);
	}
	return NULL;
}

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
	struct monitor * m = p;
	for (;;) {
		skynet_updatetime();
		CHECK_ABORT
		wakeup(m,1);
		usleep(2500);
	}
	return NULL;
}

static void *
_worker(void *p) {
	struct worker_parm *wp = p;
	int id = wp->id;
	struct monitor *m = wp->m;
	struct skynet_monitor *sm = m->m[id];
	for (;;) {
		if (skynet_context_message_dispatch(sm)) {
			CHECK_ABORT
			pthread_mutex_lock(&m->mutex);
			++ m->sleep;
			pthread_cond_wait(&m->cond, &m->mutex);
			-- m->sleep;
			pthread_mutex_unlock(&m->mutex);
		} 
	}
	return NULL;
}

static void
_start(int thread) {
	pthread_t pid[thread+3];

	struct monitor *m = malloc(sizeof(*m));
	memset(m, 0, sizeof(*m));
	m->count = thread;
	m->sleep = 0;

	m->m = malloc(thread * sizeof(struct skynet_monitor *));
	int i;
	for (i=0;i<thread;i++) {
		m->m[i] = skynet_monitor_new(i);
	}
	pthread_mutex_init(&m->mutex, NULL);
	pthread_cond_init(&m->cond, NULL);

	pthread_create(&pid[0], NULL, _monitor, m);
	pthread_create(&pid[1], NULL, _timer, m);
	pthread_create(&pid[2], NULL, _socket, m);

	struct worker_parm wp[thread];
	for (i=0;i<thread;i++) {
		wp[i].m = m;
		wp[i].id = i;
		pthread_create(&pid[i+3], NULL, _worker, &wp[i]);
	}

	for (i=1;i<thread+3;i++) {
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
	skynet_socket_init();

	if (config->standalone) {
		if (_start_master(config->standalone)) {
			fprintf(stderr, "Init fail : mater");
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
	skynet_socket_free();
}

