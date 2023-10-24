#include "skynet.h"
#include "skynet_server.h"
#include "skynet_imp.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_module.h"
#include "skynet_timer.h"
#include "skynet_monitor.h"
#include "skynet_socket.h"
#include "skynet_daemon.h"
#include "skynet_harbor.h"

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

struct monitor {
	int count;
	struct skynet_monitor ** m;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	pthread_cond_t timecond;
	pthread_mutex_t timemutex;
	pthread_cond_t workcond;
	int sleep;
	int quit;
	uint64_t start_time;
	uint64_t fast_time;
	uint32_t once_addtime;
};

struct worker_parm {
	struct monitor *m;
	int id;
	int weight;
};

static struct monitor *M;
static volatile int SIG = 0;

static void
handle_hup(int signal) {
	if (signal == SIGHUP) {
		SIG = 1;
	}
}

#define CHECK_ABORT if (skynet_context_total()==0) break;

static void
create_thread(pthread_t *thread, void *(*start_routine) (void *), void *arg) {
	if (pthread_create(thread,NULL, start_routine, arg)) {
		fprintf(stderr, "Create thread failed");
		exit(1);
	}
}

static void
wakeup(struct monitor *m, int busy) {
	if (m->sleep >= m->count - busy) {
		// signal sleep worker, "spurious wakeup" is harmless
		pthread_cond_signal(&m->cond);
	}
}

static void *
thread_socket(void *p) {
	struct monitor * m = p;
	skynet_initthread(THREAD_SOCKET);
	for (;;) {
		int r = skynet_socket_poll();
		if (r==0)
			break;
		if (r<0) {
			CHECK_ABORT
			continue;
		}
		wakeup(m,0);
	}
	return NULL;
}

static void
free_monitor(struct monitor *m) {
	int i;
	int n = m->count;
	for (i=0;i<n;i++) {
		skynet_monitor_delete(m->m[i]);
	}
	pthread_mutex_destroy(&m->mutex);
	pthread_cond_destroy(&m->cond);
	skynet_free(m->m);
	skynet_free(m);
}

static void *
thread_monitor(void *p) {
	struct monitor * m = p;
	int i;
	int n = m->count;
	skynet_initthread(THREAD_MONITOR);
	for (;;) {
		CHECK_ABORT
		for (i=0;i<n;i++) {
			skynet_monitor_check(m->m[i]);
		}
		for (i=0;i<5;i++) {
			CHECK_ABORT
			sleep(1);
		}
	}

	return NULL;
}

static void
signal_hup() {
	// make log file reopen

	struct skynet_message smsg;
	smsg.source = 0;
	smsg.session = 0;
	smsg.data = NULL;
	smsg.sz = (size_t)PTYPE_SYSTEM << MESSAGE_TYPE_SHIFT;
	uint32_t logger = skynet_handle_findname("logger");
	if (logger) {
		skynet_context_push(logger, &smsg);
	}
}

static void *
thread_timer(void *p) {
	struct monitor * m = p;
	skynet_initthread(THREAD_TIMER);
	for (;;) {
		skynet_updatetime();
		skynet_socket_updatetime();
		CHECK_ABORT
		wakeup(m,m->count-1);
		if (SIG) {
			signal_hup();
			SIG = 0;
		}
		pthread_mutex_lock(&m->timemutex);
		pthread_cond_wait(&m->timecond, &m->timemutex);
		pthread_mutex_unlock(&m->timemutex);
	}
	// wakeup socket thread
	skynet_socket_exit();
	// wakeup all worker thread
	pthread_mutex_lock(&m->mutex);
	m->quit = 1;
	pthread_cond_broadcast(&m->cond);
	pthread_mutex_unlock(&m->mutex);
	return NULL;
}

static void *
thread_fasttimer(void *p) {
	struct monitor * m = p;
	skynet_initthread(THREAD_FAST_TIMER);
	uint64_t remain_time;
	uint32_t once_addtime;
	for (;;) {
		CHECK_ABORT
		if (m->fast_time > 0) {
			pthread_mutex_lock(&m->timemutex);
			skynet_error(NULL,"fasttime begin");
			for(;;) {
				remain_time = m->fast_time - (M->start_time + skynet_now());
				if (remain_time <= 0)break;

				if (remain_time > m->once_addtime) {
					once_addtime = m->once_addtime;
				} else {
					once_addtime = remain_time;
				}
				skynet_time_fast(once_addtime);
				skynet_updatetime();
				skynet_socket_updatetime();
				pthread_mutex_lock(&m->mutex);
				pthread_cond_broadcast(&m->cond);
				pthread_cond_wait(&m->workcond, &m->mutex);
				pthread_mutex_unlock(&m->mutex);
			}
			m->fast_time = 0;
			m->once_addtime = 0;
			skynet_error(NULL,"fasttime end");
			pthread_mutex_unlock(&m->timemutex);
		}

		pthread_cond_signal(&m->timecond);
		usleep(2500);
	}

	return NULL;
}

static void *
thread_worker(void *p) {
	struct worker_parm *wp = p;
	int id = wp->id;
	int weight = wp->weight;
	struct monitor *m = wp->m;
	struct skynet_monitor *sm = m->m[id];
	skynet_initthread(THREAD_WORKER);
	struct message_queue * q = NULL;
	while (!m->quit) {
		q = skynet_context_message_dispatch(sm, q, weight);
		if (q == NULL) {
			if (pthread_mutex_lock(&m->mutex) == 0) {
				++ m->sleep;
				if (m->sleep == m->count) {
					pthread_cond_signal(&m->workcond);
				}
				// "spurious wakeup" is harmless,
				// because skynet_context_message_dispatch() can be call at any time.	
				if (!m->quit)
					pthread_cond_wait(&m->cond, &m->mutex);
				-- m->sleep;
				if (pthread_mutex_unlock(&m->mutex)) {
					fprintf(stderr, "unlock mutex error");
					exit(1);
				}
			}
		}
	}
	return NULL;
}

static void
start(int thread) {
	pthread_t pid[thread+3];

	struct monitor *m = skynet_malloc(sizeof(*m));
	M = m;
	memset(m, 0, sizeof(*m));
	m->count = thread;
	m->sleep = 0;
	m->fast_time = 0;
	m->once_addtime = 0;
	m->start_time = skynet_starttime();
	m->start_time *= 100;

	m->m = skynet_malloc(thread * sizeof(struct skynet_monitor *));
	int i;
	for (i=0;i<thread;i++) {
		m->m[i] = skynet_monitor_new();
	}
	if (pthread_mutex_init(&m->mutex, NULL)) {
		fprintf(stderr, "Init mutex error");
		exit(1);
	}
	if (pthread_cond_init(&m->cond, NULL)) {
		fprintf(stderr, "Init cond error");
		exit(1);
	}
	if (pthread_mutex_init(&m->timemutex, NULL)) {
		fprintf(stderr, "Init mutex error");
		exit(1);
	}
	if (pthread_cond_init(&m->timecond, NULL)) {
		fprintf(stderr, "Init timecond error");
		exit(1);
	}
	if (pthread_cond_init(&m->workcond, NULL)) {
		fprintf(stderr, "Init workcond error");
		exit(1);
	}

	create_thread(&pid[0], thread_monitor, m);
	create_thread(&pid[1], thread_timer, m);
	create_thread(&pid[2], thread_socket, m);
	create_thread(&pid[3], thread_fasttimer, m);

	static int weight[] = { 
		-1, -1, -1, -1, 0, 0, 0, 0,
		1, 1, 1, 1, 1, 1, 1, 1, 
		2, 2, 2, 2, 2, 2, 2, 2, 
		3, 3, 3, 3, 3, 3, 3, 3, };
	struct worker_parm wp[thread];
	for (i=0;i<thread;i++) {
		wp[i].m = m;
		wp[i].id = i;
		if (i < sizeof(weight)/sizeof(weight[0])) {
			wp[i].weight= weight[i];
		} else {
			wp[i].weight = 0;
		}
		create_thread(&pid[i+4], thread_worker, &wp[i]);
	}

	for (i=0;i<thread+4;i++) {
		pthread_join(pid[i], NULL); 
	}

	free_monitor(m);
}

static void
bootstrap(struct skynet_context * logger, const char * cmdline) {
	int sz = strlen(cmdline);
	char name[sz+1];
	char args[sz+1];
	int arg_pos;
	sscanf(cmdline, "%s", name);  
	arg_pos = strlen(name);
	if (arg_pos < sz) {
		while(cmdline[arg_pos] == ' ') {
			arg_pos++;
		}
		strncpy(args, cmdline + arg_pos, sz);
	} else {
		args[0] = '\0';
	}
	struct skynet_context *ctx = skynet_context_new(name, args);
	if (ctx == NULL) {
		skynet_error(NULL, "Bootstrap error : %s\n", cmdline);
		skynet_context_dispatchall(logger);
		exit(1);
	}
}

void 
skynet_start(struct skynet_config * config) {
	// register SIGHUP for log file reopen
	struct sigaction sa;
	sa.sa_handler = &handle_hup;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);

	if (config->daemon) {
		if (daemon_init(config->daemon)) {
			exit(1);
		}
	}
	skynet_harbor_init(config->harbor);
	skynet_handle_init(config->harbor);
	skynet_mq_init();
	skynet_module_init(config->module_path);
	skynet_timer_init();
	skynet_socket_init();
	skynet_profile_enable(config->profile);

	struct skynet_context *ctx = skynet_context_new(config->logservice, config->logger);
	if (ctx == NULL) {
		fprintf(stderr, "Can't launch %s service\n", config->logservice);
		exit(1);
	}

	skynet_handle_namehandle(skynet_context_handle(ctx), "logger");

	bootstrap(ctx, config->bootstrap);

	start(config->thread);

	// harbor_exit may call socket send, so it should exit before socket_free
	skynet_harbor_exit();
	skynet_socket_free();
	if (config->daemon) {
		daemon_exit(config->daemon);
	}
}


uint64_t skynet_fast_time(uint64_t ftime, uint32_t once_add) {
	pthread_mutex_lock(&M->timemutex);
	uint64_t now_time = M->start_time + skynet_now();
	if (ftime < now_time && once_add > 0) {
		skynet_error(NULL,"fasttime must be greater than the current time now_time= %lld fasttime = %lld once_add=%u",now_time,ftime,once_add);
		pthread_mutex_unlock(&M->timemutex);
		return 0;
	}
	M->fast_time = ftime;
	M->once_addtime = once_add;
	pthread_mutex_unlock(&M->timemutex);
	return ftime;
}
