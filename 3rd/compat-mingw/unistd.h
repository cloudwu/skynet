#pragma once

#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <process.h>
#include <io.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

// Define missing errno values for network errors
#ifndef ECONNRESET
#define ECONNRESET 104
#endif
#ifndef ECONNABORTED
#define ECONNABORTED 103
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED 111
#endif
#ifndef ENETDOWN
#define ENETDOWN 100
#endif
#ifndef ENETUNREACH
#define ENETUNREACH 101
#endif
#ifndef EHOSTDOWN
#define EHOSTDOWN 112
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH 113
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif
#ifndef ENOTCONN
#define ENOTCONN 107
#endif
#ifndef EADDRINUSE
#define EADDRINUSE 98
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL 99
#endif

// Include winsock2.h for gethostname and other network functions
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

// Undefine Windows legacy keywords that conflict with variable names
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

// Socket compatibility macros
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

#define ssize_t size_t

#define random rand
#define srandom srand
#define snprintf _snprintf
#define localtime_r _localtime64_s

#define pid_t int

int kill(pid_t pid, int exit_code);

void usleep(size_t us);
void sleep(size_t ms);

int clock_gettime(int what, struct timespec *ti);

enum { LOCK_EX, LOCK_NB };
int flock(int fd, int flag);

struct sigaction {
  void (*sa_handler)(int);
  int sa_flags;
  int sa_mask;
};
enum { SIGPIPE, SIGHUP, SA_RESTART };
void sigfillset(int *flag);
int sigemptyset(int* set);
void sigaction(int flag, struct sigaction *action, void* param);

int pipe(int fd[2]);
int daemon(int a, int b);

#define O_NONBLOCK 1
#define F_SETFL 0
#define F_GETFL 1

int fcntl(int fd, int cmd, long arg);

char *strsep(char **stringp, const char *delim);

int write(int fd, const void* ptr, unsigned int sz);
int read(int fd, void* buffer, unsigned int sz);
// Wrapper function for recv with better error handling
int compat_recv(SOCKET s, char *buf, int len, int flags);

// Macro to redirect recv calls to our wrapper
#define recv compat_recv
int close(int fd);

#define getpid _getpid
#define open _open
#define dup2 _dup2
