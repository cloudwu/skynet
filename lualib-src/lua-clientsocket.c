// simple lua socket library for client
// It's only for demo, limited feature. Don't use it in your project.
// Rewrite socket library by yourself .

#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define CACHE_SIZE 0x1000	

static int
lconnect(lua_State *L) {
	const char * addr = luaL_checkstring(L, 1);
	int port = luaL_checkinteger(L, 2);
	int fd = socket(AF_INET,SOCK_STREAM,0);
	struct sockaddr_in my_addr;

	my_addr.sin_addr.s_addr=inet_addr(addr);
	my_addr.sin_family=AF_INET;
	my_addr.sin_port=htons(port);

	int r = connect(fd,(struct sockaddr *)&my_addr,sizeof(struct sockaddr_in));

	if (r == -1) {
		return luaL_error(L, "Connect %s %d failed", addr, port);
	}

	int flag = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);

	lua_pushinteger(L, fd);

	return 1;
}

static int
lclose(lua_State *L) {
	int fd = luaL_checkinteger(L, 1);
	close(fd);

	return 0;
}

static void
block_send(lua_State *L, int fd, const char * buffer, int sz) {
	while(sz > 0) {
		int r = send(fd, buffer, sz, 0);
		if (r < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			luaL_error(L, "socket error: %s", strerror(errno));
		}
		buffer += r;
		sz -= r;
	}
}

/*
	integer fd
	string message
 */
static int
lsend(lua_State *L) {
	size_t sz = 0;
	int fd = luaL_checkinteger(L,1);
	const char * msg = luaL_checklstring(L, 2, &sz);

	block_send(L, fd, msg, (int)sz);

	return 0;
}

/*
	intger fd
	string last
	table result

	return 
		boolean (true: data, false: block, nil: close)
		string last
 */

struct socket_buffer {
	void * buffer;
	int sz;
};

static int
lrecv(lua_State *L) {
	int fd = luaL_checkinteger(L,1);

	char buffer[CACHE_SIZE];
	int r = recv(fd, buffer, CACHE_SIZE, 0);
	if (r == 0) {
		lua_pushliteral(L, "");
		// close
		return 1;
	}
	if (r < 0) {
		if (errno == EAGAIN || errno == EINTR) {
			return 0;
		}
		luaL_error(L, "socket error: %s", strerror(errno));
	}
	lua_pushlstring(L, buffer, r);
	return 1;
}

static int
lusleep(lua_State *L) {
	int n = luaL_checknumber(L, 1);
	usleep(n);
	return 0;
}

// quick and dirty none block stdin readline

#define QUEUE_SIZE 1024

struct queue {
	pthread_mutex_t lock;
	int head;
	int tail;
	char * queue[QUEUE_SIZE];
};

static void *
readline_stdin(void * arg) {
	struct queue * q = arg;
	char tmp[1024];
	while (!feof(stdin)) {
		if (fgets(tmp,sizeof(tmp),stdin) == NULL) {
			// read stdin failed
			exit(1);
		}
		int n = strlen(tmp) -1;

		char * str = malloc(n+1);
		memcpy(str, tmp, n);
		str[n] = 0;

		pthread_mutex_lock(&q->lock);
		q->queue[q->tail] = str;

		if (++q->tail >= QUEUE_SIZE) {
			q->tail = 0;
		}
		if (q->head == q->tail) {
			// queue overflow
			exit(1);
		}
		pthread_mutex_unlock(&q->lock);
	}
	return NULL;
}

static int
lreadstdin(lua_State *L) {
	struct queue *q = lua_touserdata(L, lua_upvalueindex(1));
	pthread_mutex_lock(&q->lock);
	if (q->head == q->tail) {
		pthread_mutex_unlock(&q->lock);
		return 0;
	}
	char * str = q->queue[q->head];
	if (++q->head >= QUEUE_SIZE) {
		q->head = 0;
	}
	pthread_mutex_unlock(&q->lock);
	lua_pushstring(L, str);
	free(str);
	return 1;
}

LUAMOD_API int
luaopen_clientsocket(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "connect", lconnect },
		{ "recv", lrecv },
		{ "send", lsend },
		{ "close", lclose },
		{ "usleep", lusleep },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);

	struct queue * q = lua_newuserdata(L, sizeof(*q));
	memset(q, 0, sizeof(*q));
	pthread_mutex_init(&q->lock, NULL);
	lua_pushcclosure(L, lreadstdin, 1);
	lua_setfield(L, -2, "readstdin");

	pthread_t pid ;
	pthread_create(&pid, NULL, readline_stdin, q);

	return 1;
}
