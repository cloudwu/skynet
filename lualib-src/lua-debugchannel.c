#define LUA_LIB

// only for debug use
#include <lua.h>
#include <lauxlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "spinlock.h"

#define METANAME "debugchannel"

struct command {
	struct command * next;
	size_t sz;
};

struct channel {
	struct spinlock lock;
	int ref;
	struct command * head;
	struct command * tail;
};

static struct channel *
channel_new() {
	struct channel * c = malloc(sizeof(*c));
	memset(c, 0 , sizeof(*c));
	c->ref = 1;
	SPIN_INIT(c)

	return c;
}

static struct channel *
channel_connect(struct channel *c) {
	struct channel * ret = NULL;
	SPIN_LOCK(c)
	if (c->ref == 1) {
		++c->ref;
		ret = c;
	}
	SPIN_UNLOCK(c)
	return ret;
}

static struct channel *
channel_release(struct channel *c) {
	SPIN_LOCK(c)
	--c->ref;
	if (c->ref > 0) {
		SPIN_UNLOCK(c)
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
	SPIN_UNLOCK(c)
	SPIN_DESTROY(c)
	free(c);
	return NULL;
}

// call free after channel_read
static struct command *
channel_read(struct channel *c, double timeout) {
	struct command * ret = NULL;
	SPIN_LOCK(c)
	if (c->head == NULL) {
		SPIN_UNLOCK(c)
		int ti = (int)(timeout * 100000);
		usleep(ti);
		return NULL;
	}
	ret = c->head;
	c->head = ret->next;
	if (c->head == NULL) {
		c->tail = NULL;
	}
	SPIN_UNLOCK(c)
	
	return ret;
}

static void
channel_write(struct channel *c, const char * s, size_t sz) {
	struct command * cmd = malloc(sizeof(*cmd)+ sz);
	cmd->sz = sz;
	cmd->next = NULL;
	memcpy(cmd+1, s, sz);
	SPIN_LOCK(c)
	if (c->tail == NULL) {
		c->head = c->tail = cmd;
	} else {
		c->tail->next = cmd;
		c->tail = cmd;
	}
	SPIN_UNLOCK(c)
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

static const int HOOKKEY = 0;

/*
** Auxiliary function used by several library functions: check for
** an optional thread as function's first argument and set 'arg' with
** 1 if this argument is present (so that functions can skip it to
** access their other arguments)
*/
static lua_State *getthread (lua_State *L, int *arg) {
  if (lua_isthread(L, 1)) {
    *arg = 1;
    return lua_tothread(L, 1);
  }
  else {
    *arg = 0;
    return L;  /* function will operate over current thread */
  }
}

/*
** Call hook function registered at hook table for the current
** thread (if there is one)
*/
static void hookf (lua_State *L, lua_Debug *ar) {
  static const char *const hooknames[] =
    {"call", "return", "line", "count", "tail call"};
  lua_rawgetp(L, LUA_REGISTRYINDEX, &HOOKKEY);
  lua_pushthread(L);
  if (lua_rawget(L, -2) == LUA_TFUNCTION) {  /* is there a hook function? */
    lua_pushstring(L, hooknames[(int)ar->event]);  /* push event name */
    if (ar->currentline >= 0)
      lua_pushinteger(L, ar->currentline);  /* push current line */
    else lua_pushnil(L);
    lua_call(L, 2, 1);  /* call hook function */
	int yield = lua_toboolean(L, -1);
	lua_pop(L,1);
	if (yield) {
		lua_yield(L, 0);
	}
  }
}

/*
** Convert a string mask (for 'sethook') into a bit mask
*/
static int makemask (const char *smask, int count) {
  int mask = 0;
  if (strchr(smask, 'c')) mask |= LUA_MASKCALL;
  if (strchr(smask, 'r')) mask |= LUA_MASKRET;
  if (strchr(smask, 'l')) mask |= LUA_MASKLINE;
  if (count > 0) mask |= LUA_MASKCOUNT;
  return mask;
}

static int db_sethook (lua_State *L) {
  int arg, mask, count;
  lua_Hook func;
  lua_State *L1 = getthread(L, &arg);
  if (lua_isnoneornil(L, arg+1)) {  /* no hook? */
    lua_settop(L, arg+1);
    func = NULL; mask = 0; count = 0;  /* turn off hooks */
  }
  else {
    const char *smask = luaL_checkstring(L, arg+2);
    luaL_checktype(L, arg+1, LUA_TFUNCTION);
    count = (int)luaL_optinteger(L, arg + 3, 0);
    func = hookf; mask = makemask(smask, count);
  }
  if (lua_rawgetp(L, LUA_REGISTRYINDEX, &HOOKKEY) == LUA_TNIL) {
    lua_createtable(L, 0, 2);  /* create a hook table */
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &HOOKKEY);  /* set it in position */
    lua_pushstring(L, "k");
    lua_setfield(L, -2, "__mode");  /** hooktable.__mode = "k" */
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -2);  /* setmetatable(hooktable) = hooktable */
  }
  lua_pushthread(L1); lua_xmove(L1, L, 1);  /* key (thread) */
  lua_pushvalue(L, arg + 1);  /* value (hook function) */
  lua_rawset(L, -3);  /* hooktable[L1] = new Lua hook */
  lua_sethook(L1, func, mask, count);
  return 0;
}

LUAMOD_API int
luaopen_skynet_debugchannel(lua_State *L) {
	luaL_Reg l[] = {
		{ "create", lcreate },	// for write
		{ "connect", lconnect },	// for read
		{ "release", lrelease },
		{ "sethook", db_sethook },
		{ NULL, NULL },
	};
	luaL_checkversion(L);
	luaL_newlib(L,l);
	return 1;
}
