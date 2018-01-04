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
	int sleep;
	int quit;
};

struct worker_parm {
	struct monitor *m;
	int id;
	int weight;
};

static int SIG = 0;

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
		CHECK_ABORT
		wakeup(m,m->count-1);
		usleep(2500);
		if (SIG) {
			signal_hup();
			SIG = 0;
		}
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
thread_worker(void *p) {
	struct worker_parm *wp = p;
	int id = wp->id;
	int weight = wp->weight;
	struct monitor *m = wp->m;
	struct skynet_monitor *sm = m->m[id];
	skynet_initthread(THREAD_WORKER);
	struct message_queue * q = NULL;                            //服务私有消息队列
	while (!m->quit) {
		q = skynet_context_message_dispatch(sm, q, weight);     //消息队列的派发和处理
		if (q == NULL) {                                        //消息队列无消息可执行则挂起当前工作线程
			if (pthread_mutex_lock(&m->mutex) == 0) {           //上互斥锁 成功返回0
				++ m->sleep;
				// "spurious wakeup" is harmless,
				// because skynet_context_message_dispatch() can be call at any time.
				if (!m->quit)
					pthread_cond_wait(&m->cond, &m->mutex);
				-- m->sleep;
				if (pthread_mutex_unlock(&m->mutex)) {          //释放互斥锁
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
	memset(m, 0, sizeof(*m));
	m->count = thread;
	m->sleep = 0;

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

	create_thread(&pid[0], thread_monitor, m);
	create_thread(&pid[1], thread_timer, m);
	create_thread(&pid[2], thread_socket, m);

	static int weight[] = { 
		-1, -1, -1, -1, 0, 0, 0, 0,
		1, 1, 1, 1, 1, 1, 1, 1, 
		2, 2, 2, 2, 2, 2, 2, 2, 
		3, 3, 3, 3, 3, 3, 3, 3, };
	struct worker_parm wp[thread];
	for (i=0;i<thread;i++) {
		wp[i].m = m;
		wp[i].id = i;
        //第i个线程的weight为：当i小于weight数组长度时，线程weight为weight[i]，否则为0
        //每个线程一次处理的消息个数由工作线程配置的权重决定。
		if (i < sizeof(weight)/sizeof(weight[0])) {
			wp[i].weight= weight[i];
		} else {
			wp[i].weight = 0;
		}
		create_thread(&pid[i+3], thread_worker, &wp[i]);
	}

	for (i=0;i<thread+3;i++) {
		pthread_join(pid[i], NULL); 
	}

	free_monitor(m);
}
/**
 * 	bootstrap(ctx, config->bootstrap);
 * @param logger
 * @param cmdline : "snlua bootstrap"
 */
static void
bootstrap(struct skynet_context * logger, const char * cmdline) {
	int sz = strlen(cmdline);  //获取字符串长度
	char name[sz+1];
	char args[sz+1];
	sscanf(cmdline, "%s %s", name, args); //将传入的cmdline字符串按照格式分割成两部分，前部分模块名，后部分为模块初始化参数
	struct skynet_context *ctx = skynet_context_new(name, args);  //skynet_context_new("snlua", "boostrap")创建并启动指定模块的一个服务
	if (ctx == NULL) {  //假如创建失败
		skynet_error(NULL, "Bootstrap error : %s\n", cmdline); //通过传入的logger服务接口构建错误信息假如logger消息队列
		skynet_context_dispatchall(logger);   //输出消息队列中的错误信息
		exit(1);
	}
}

void 
skynet_start(struct skynet_config * config) {
	// register SIGHUP for log file reopen
	struct sigaction sa;
	sa.sa_handler = &handle_hup;    // Setup the handler function
	sa.sa_flags = SA_RESTART;       // Restart the system call, if at all possible
	sigfillset(&sa.sa_mask);        // Block every signal during the handler
	sigaction(SIGHUP, &sa, NULL);   // SIGHUP:挂起信号 sa：新的处理函数， NULL：不保留旧的处理函数

	if (config->daemon) {
		if (daemon_init(config->daemon)) {  //skynet_daemon.c 初始化守护进程，由配置文件指定是否调用
			exit(1);
		}
	}
	skynet_harbor_init(config->harbor);         //skynet_harbor.c初始化节点模块，用于集群，转发远程节点消息
	skynet_handle_init(config->harbor);         //skynet_handle.c初始化句柄模块
	skynet_mq_init();                           //skynet_mq.c初始化消息队列模块
	skynet_module_init(config->module_path);    //skynet_module.c初始化服务动态库加载模块, 加载符合skynet服务模块接口的动态链接库（.so）
	skynet_timer_init();                        //skynet_timer.c定时器模块
	skynet_socket_init();                       //skynet_socket.c网络通信模块
	skynet_profile_enable(config->profile);

	struct skynet_context *ctx = skynet_context_new(config->logservice, config->logger);   //skynet_server.c加载日志模块(系统启动的第一个模块)
	if (ctx == NULL) {
		fprintf(stderr, "Can't launch %s service\n", config->logservice);
		exit(1);
	}
    //启动的服务是bootstrap，但先要加载snlua模块，所有的lua服务都属于snlua模块的实例。
    //加载引导模块 默认 config->bootstrap = "snlua bootstrap",并使用snlua服务启动bootstrap.lua脚本
    //不使用snlua也可以直接启动其他服务的动态库
	bootstrap(ctx, config->bootstrap);

	start(config->thread);                       // start worker thread

	// harbor_exit may call socket send, so it should exit before socket_free
	skynet_harbor_exit();
	skynet_socket_free();
	if (config->daemon) {
		daemon_exit(config->daemon);
	}
}
