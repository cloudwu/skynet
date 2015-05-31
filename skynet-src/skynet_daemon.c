// stdio 就是指 “standard input & output"
#include <stdio.h>

// unistd.h 是 C 和 C++ 程序设计语言中提供对 POSIX 操作系统 API 的访问功能的头文件的名称。
// 对于类 Unix 系统，unistd.h 中所定义的接口通常都是大量针对系统调用的封装.
#include <unistd.h>

// sys/types.h中文名称为基本系统数据类型。
// 在应用程序源文件中包含 <sys/types.h> 以访问 _LP64 和 _ILP32 的定义。此头文件还包含适当时应使用的多个基本派生类型。
#include <sys/types.h>

// sys/file.h 包含了一些其他的头文件
#include <sys/file.h>

// signal.h是C标准函数库中的信号处理部分， 定义了程序执行时如何处理不同的信号。
// 信号用作进程间通信， 报告异常行为（如除零）、用户的一些按键组合（如同时按下Ctrl与C键，产生信号SIGINT）。
#include <signal.h>

// errno.h 是C语言C标准函式库里的标头档，定义了通过错误码来回报错误资讯的宏：
#include <errno.h>

// stdlib 头文件即standard library标准库头文件。stdlib.h里面定义了五种类型、一些宏和通用工具函数。
#include <stdlib.h>

#include "skynet_daemon.h"

/**
 * 检测 pidfile 里面配置的 pid 是否可用
 * @param pidfile 配置守护进程的文件
 * @return 如果可用, 则返回配置的 pid, 否则返回 0
 */
static int
check_pid(const char *pidfile) {
	int pid = 0;
	FILE *f = fopen(pidfile,"r");
	if (f == NULL)
		return 0;
	int n = fscanf(f,"%d", &pid);
	fclose(f);

	if (n != 1	// fscanf 参数填充的个数, 保证读入的参数个数为 1
		|| pid == 0 // 读取到的 pid 数据不能是 0
		|| pid == getpid()) // 不能和当前进程的 ID 相同, getpid() 取得进程识别码, http://baike.baidu.com/view/1745430.htm
	{
		return 0;
	}

	// kill, 传送信号给指定的进程，使用 kill -l 命令可查看linux系统中信号。
	// http://baike.baidu.com/subview/22085/15845697.htm#viewPageContent
	// ESRCH, 参数pid 所指定的进程或进程组不存在
	if (kill(pid, 0) && errno == ESRCH)
		return 0;

	return pid;
}

/**
 * 
 */
static int 
write_pid(const char *pidfile) {
	FILE *f;
	int pid = 0;

	// open是UNIX系统（包括LINUX、Mac等）的系统调用函数，区别于C语言库函数fopen。
	// http://baike.baidu.com/subview/26337/12488714.htm
	// fopen 和 open 的区别: http://www.cnblogs.com/joeblackzqq/archive/2011/04/11/2013010.html
	int fd = open(pidfile, O_RDWR|O_CREAT, 0644);
	if (fd == -1) {
		fprintf(stderr, "Can't create %s.\n", pidfile);
		return 0;
	}

	// 
	f = fdopen(fd, "r+");
	if (f == NULL) {
		fprintf(stderr, "Can't open %s.\n", pidfile);
		return 0;
	}

	if (flock(fd, LOCK_EX|LOCK_NB) == -1) {
		int n = fscanf(f, "%d", &pid);
		fclose(f);
		if (n != 1) {
			fprintf(stderr, "Can't lock and read pidfile.\n");
		} else {
			fprintf(stderr, "Can't lock pidfile, lock is held by pid %d.\n", pid);
		}
		return 0;
	}
	
	pid = getpid();
	if (!fprintf(f,"%d\n", pid)) {
		fprintf(stderr, "Can't write pid.\n");
		close(fd);
		return 0;
	}
	fflush(f);

	return pid;
}

/**
 * 守护进程初始化
 * @param pidfile 配置守护进程的文件
 */
int
daemon_init(const char *pidfile) {
	int pid = check_pid(pidfile);

	if (pid) {
		fprintf(stderr, "Skynet is already running, pid = %d.\n", pid);
		return 1;
	}

#ifdef __APPLE__
	fprintf(stderr, "'daemon' is deprecated: first deprecated in OS X 10.5 , use launchd instead.\n");
#else
	if (daemon(1,0)) {
		fprintf(stderr, "Can't daemonize.\n");
		return 1;
	}
#endif

	pid = write_pid(pidfile);
	if (pid == 0) {
		return 1;
	}

	return 0;
}

int 
daemon_exit(const char *pidfile) {
	return unlink(pidfile);
}
