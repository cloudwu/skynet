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

// 监控
struct monitor {
	int count;					// 工作者线程数 skynet内部实际上市 count + 3 多了3个线程的
	struct skynet_monitor ** m; // monitor 工作线程监控表
	pthread_cond_t  cond;  		// 条件变量
	pthread_mutex_t mutex; 		// 互斥锁        条件变量和互斥锁实现线程的同步
	int sleep;					// 睡眠中的工作者线程数
};

// 用于线程参数 工作线程
struct worker_parm {
	struct monitor *m;
	int id;
};

#define CHECK_ABORT if (skynet_context_total()==0) break; // 服务数为0

static void
create_thread(pthread_t *thread, void *(*start_routine) (void *), void *arg) {
	if (pthread_create(thread,NULL, start_routine, arg)) {
		fprintf(stderr, "Create thread failed");
		exit(1);
	}
}

// 全部线程都睡眠的情况下才唤醒一个工作线程(即只要有工作线程处于工作状态，则不需要唤醒)
static void
wakeup(struct monitor *m, int busy) {
	if (m->sleep >= m->count - busy) { // 睡眠的线程
		// signal sleep worker, "spurious wakeup" is harmless
		pthread_cond_signal(&m->cond);
	}
}

static void *
_socket(void *p) {
	struct monitor * m = p;
	for (;;) {
		int r = skynet_socket_poll();
		if (r==0)		// SOCKET_EXIT
			break;
		if (r<0) {
			CHECK_ABORT
			continue;
		}
		// 有socket消息返回
		wakeup(m,0);	// 全部线程都睡眠的情况下才唤醒一个工作线程(即只要有工作线程处于工作状态，则不需要唤醒)
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
	free(m->m);
	free(m);
}

// 用于监控是否有消息没有即时处理
static void *
_monitor(void *p) {
	struct monitor * m = p;
	int i;
	int n = m->count;
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

// 用于定时器
static void *
_timer(void *p) {
	struct monitor * m = p;
	for (;;) {
		skynet_updatetime();
		CHECK_ABORT
		wakeup(m,m->count-1);	// 只要有一个睡眠线程就唤醒，让工作线程热起来
		usleep(2500);
	}

	// wakeup socket thread
	skynet_socket_exit();

	// wakeup all worker thread
	pthread_cond_broadcast(&m->cond);
	return NULL;
}

// 工作线程
static void *
_worker(void *p) {
	struct worker_parm *wp = p;
	int id = wp->id;
	struct monitor *m = wp->m;
	struct skynet_monitor *sm = m->m[id];
	for (;;) {
		if (skynet_context_message_dispatch(sm)) {
			CHECK_ABORT
			if (pthread_mutex_lock(&m->mutex) == 0) {
				++ m->sleep;

				// 假装的醒来时无害的 因为 skynet_ctx_msg_dispatch() 可以在任何时候被调用
				// "spurious wakeup" is harmless,
				// because skynet_context_message_dispatch() can be call at any time.
				pthread_cond_wait(&m->cond, &m->mutex); // wait for wakeup
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
_start(int thread) {
	pthread_t pid[thread+3]; // 线程数+3 3个线程分别用于 _monitor _timer  _socket 监控 定时器 socket IO

	struct monitor *m = malloc(sizeof(*m));
	memset(m, 0, sizeof(*m));
	m->count = thread;
	m->sleep = 0;

	m->m = malloc(thread * sizeof(struct skynet_monitor *));
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

	create_thread(&pid[0], _monitor, m);
	create_thread(&pid[1], _timer, m);
	create_thread(&pid[2], _socket, m);

	struct worker_parm wp[thread];
	for (i=0;i<thread;i++) {
		wp[i].m = m;
		wp[i].id = i;
		create_thread(&pid[i+3], _worker, &wp[i]);
	}

	for (i=0;i<thread+3;i++) {
		pthread_join(pid[i], NULL); // 等待所有线程退出
	}

	free_monitor(m); // 释放监控
}

static int
_start_master(const char * master) {
	struct skynet_context *ctx = skynet_context_new("master", master);
	if (ctx == NULL)
		return 1;
	return 0;	
}

// skynet 启动的时候 初始化
void 
skynet_start(struct skynet_config * config) {
	skynet_group_init();
	skynet_harbor_init(config->harbor);
	skynet_handle_init(config->harbor);
	skynet_mq_init();
	skynet_module_init(config->module_path);
	skynet_timer_init();
	skynet_socket_init();

	struct skynet_context *ctx;
	ctx = skynet_context_new("logger", config->logger);
	if (ctx == NULL) {
		fprintf(stderr,"launch logger error");
		exit(1);
	}

	if (config->standalone) { // 是否是单机版
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

