// only for debug use
#include <lua.h>
#include <lauxlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define METANAME "debugchannel"
#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {}
#define UNLOCK(q) __sync_lock_release(&(q)->lock);

struct command {
	struct command * next;
	size_t sz;
};

struct channel {
	int lock;
	int ref;
	struct command * head;
	struct command * tail;
};

static struct channel *
channel_new() {
	struct channel * c = malloc(sizeof(*c));
	memset(c, 0 , sizeof(*c));
	c->ref = 1;

	return c;
}

static struct channel *
channel_connect(struct channel *c) {
	struct channel * ret = NULL;
	LOCK(c)
	if (c->ref == 1) {
		++c->ref;
		ret = c;
	}
	UNLOCK(c)
	return ret;
}

static struct channel *
channel_release(struct channel *c) {
	LOCK(c)
	--c->ref;
	if (c->ref > 0) {
		UNLOCK(c)
		return c;
	}
	// never unlock while reference is 0
	struct command * p = c->head;
	c->head = NULL;
	c->tail = NULL;
	while(p) {
		struct command *next = p->next;
		free(p);
		p = next;
	}
	return NULL;
}

// call free after channel_read
static struct command *
channel_read(struct channel *c, double timeout) {
	struct command * ret = NULL;
	LOCK(c)
	if (c->head == NULL) {
		UNLOCK(c)
		int ti = (int)(timeout * 100000);
		usleep(ti);
		return NULL;
	}
	ret = c->head;
	c->head = ret->next;
	if (c->head == NULL) {
		c->tail = NULL;
	}
	UNLOCK(c)
	
	return ret;
}

static void
channel_write(struct channel *c, const char * s, size_t sz) {
	struct command * cmd = malloc(sizeof(*cmd)+ sz);
	cmd->sz = sz;
	cmd->next = NULL;
	memcpy(cmd+1, s, sz);
	LOCK(c)
	if (c->tail == NULL) {
		c->head = c->tail = cmd;
	} else {
		c->tail->next = cmd;
		c->tail = cmd;
	}
	UNLOCK(c)
}

struct channel_box {
	struct channel *c;
};

static int
lread(lua_State *L) {
	struct channel_box *cb = luaL_checkudata(L,1, METANAME);
	double ti = luaL_optnumber(L, 2, 0);
	struct command * c = channel_read(cb->c, ti);
	if (c == NULL)
		return 0;
	lua_pushlstring(L, (const char *)(c+1), c->sz);
	free(c);
	return 1;
}

static int
lwrite(lua_State *L) {
	struct channel_box *cb = luaL_checkudata(L,1, METANAME);
	size_t sz;
	const char * str = luaL_checklstring(L, 2, &sz);
	channel_write(cb->c, str, sz);
	return 0;
}

static int
lrelease(lua_State *L) {
	struct channel_box *cb = lua_touserdata(L, 1);
	if (cb) {
		if (channel_release(cb->c) == NULL) {
			cb->c = NULL;
		}
	}

	return 0;
}

static struct channel *
new_channel(lua_State *L, struct channel *c) {
	if (c == NULL) {
		c = channel_new();
	} else {
		c = channel_connect(c);
	}
	if (c == NULL) {
		luaL_error(L, "new channel failed");
		// never go here
	}
	struct channel_box * cb = lua_newuserdata(L, sizeof(*cb));
	cb->c = c;
	if (luaL_newmetatable(L, METANAME)) {
		luaL_Reg l[]={
			{ "read", lread },
			{ "write", lwrite },
			{ NULL, NULL },
		};
		luaL_newlib(L,l);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, lrelease);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);
	return c;
}

static int
lcreate(lua_State *L) {
	struct channel *c = new_channel(L, NULL);
	lua_pushlightuserdata(L, c);
	return 2;
}

static int
lconnect(lua_State *L) {
	struct channel *c = lua_touserdata(L, 1);
	if (c == NULL)
		return luaL_error(L, "Invalid channel pointer");
	new_channel(L, c);

	return 1;
}

int
luaopen_debugchannel(lua_State *L) {
	luaL_Reg l[] = {
		{ "create", lcreate },	// for write
		{ "connect", lconnect },	// for read
		{ "release", lrelease },
		{ NULL, NULL },
	};
	luaL_checkversion(L);
	luaL_newlib(L,l);
	return 1;
}
