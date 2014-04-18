// simple lua socket library for client
// It's only for demo, limited feature. Don't use it in your project.
// Rewrite socket library by yourself .

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
	uint8_t tmp[sz + 2];
	if (sz >= 0x10000) {
		return luaL_error(L, "package too long %d (16bit limited)", (int)sz);
	}
	tmp[0] = (sz >> 8) & 0xff;
	tmp[1] = sz & 0xff;
	memcpy(tmp+2, msg, sz);

	block_send(L, fd, (const char *)tmp, (int)sz+2);

	return 0;
}


static int
unpack(lua_State *L, uint8_t *buffer, int sz, int n) {
	int size = 0;
	if (sz >= 2) {
		size = buffer[0] << 8 | buffer[1];
		if (size > sz - 2) {
			goto _block;
		}
	} else {
		goto _block;
	}
	++n;
	lua_pushlstring(L, (const char *)buffer+2, size);
	lua_rawseti(L, 3, n);
	buffer += size + 2;
	sz -= size + 2;
	return unpack(L, buffer, sz, n);
_block:
	lua_pushboolean(L, n==0 ? 0:1);
	if (sz == 0) {
		lua_pushnil(L);
	} else {
		lua_pushlstring(L, (const char *)buffer, sz);
	}
	return 2;
}

/*
	intger fd
	string last
	table result

	return 
		boolean (true: data, false: block, nil: close)
		string last
 */
static int
lrecv(lua_State *L) {
	int fd = luaL_checkinteger(L,1);
	size_t sz = 0;
	const char * last = lua_tolstring(L,2,&sz);
	luaL_checktype(L, 3, LUA_TTABLE);

	char tmp[CACHE_SIZE];
	char * buffer;
	int r = recv(fd, tmp, CACHE_SIZE, 0);
	if (r == 0) {
		// close
		return 0;
	}
	if (r < 0) {
		if (errno == EAGAIN || errno == EINTR) {
			lua_pushboolean(L, 0);
			lua_pushvalue(L, 2);
			return 2;
		}
		luaL_error(L, "socket error: %s", strerror(errno));
	}
	if (sz + r <= CACHE_SIZE) {
		buffer = tmp;
		memmove(buffer + sz, buffer, r);
		memcpy(buffer, last, sz);
	} else {
		buffer = lua_newuserdata(L, r + sz);
		memcpy(buffer, last, sz);
		memcpy(buffer + sz, tmp, r);
	}

	int i;
	int n = lua_rawlen(L, 3);
	for (i=1;i<=n;i++) {
		lua_pushnil(L);
		lua_rawseti(L, 3, i);
	}

	return unpack(L, (uint8_t *)buffer, r+sz, 0);
}

static int
lusleep(lua_State *L) {
	int n = luaL_checknumber(L, 1);
	usleep(n);
	return 0;
}

// quick and dirty none block stdin readline

#define QUEUE_SIZE 1024

#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {}
#define UNLOCK(q) __sync_lock_release(&(q)->lock);

struct queue {
	int lock;
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

		LOCK(q);
		q->queue[q->tail] = str;

		if (++q->tail >= QUEUE_SIZE) {
			q->tail = 0;
		}
		if (q->head == q->tail) {
			// queue overflow
			exit(1);
		}
		UNLOCK(q);
	}
	return NULL;
}

static int
lreadline(lua_State *L) {
	struct queue *q = lua_touserdata(L, lua_upvalueindex(1));
	LOCK(q);
	if (q->head == q->tail) {
		UNLOCK(q);
		return 0;
	}
	char * str = q->queue[q->head];
	if (++q->head >= QUEUE_SIZE) {
		q->head = 0;
	}
	UNLOCK(q);
	lua_pushstring(L, str);
	free(str);
	return 1;
}

int
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
	lua_pushcclosure(L, lreadline, 1);
	lua_setfield(L, -2, "readline");

	pthread_t pid ;
	pthread_create(&pid, NULL, readline_stdin, q);

	return 1;
}
