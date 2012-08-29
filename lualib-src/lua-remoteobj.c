#include <lua.h>
#include <lauxlib.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REMOTE_OBJECT 0x40000

struct remote_address {
	int handle;
	uint32_t address;
};

struct remote_objects {
	int handle_index;
	struct remote_address addr[MAX_REMOTE_OBJECT];
};

static struct remote_objects * _R = NULL;

static struct remote_objects *
_create() {
	struct remote_objects * r = malloc(sizeof(*r));
	r->handle_index = 0;
	memset(r, 0, sizeof(*r));
	r->addr[0].address = 0xffffffff;
	return r;
}

static void
_remove(struct remote_objects *r, uint32_t address) {
	int i;
	for (i=0;i<MAX_REMOTE_OBJECT;i++) {
		if (r->addr[i].address == address) {
			r->addr[i].address = 0;
		}
	}
}

static int
_alloc(struct remote_objects *r, uint32_t address) {
	int i = 0;
	for (i=0;i<MAX_REMOTE_OBJECT;i++) {
		int handle = __sync_add_and_fetch(&r->handle_index, 1);
		struct remote_address * slot = r->addr + handle % MAX_REMOTE_OBJECT;
		if (slot->address == 0) {
			if (__sync_bool_compare_and_swap(&slot->address, 0, address)) {
				slot->handle = handle;
				return handle;
			}
		}
	}

	return -1;
}

static int
_bind(struct remote_objects *r, int handle, uint32_t address) {
	struct remote_address * slot = r->addr + handle % MAX_REMOTE_OBJECT;
	if (slot->handle != handle) {
		return 0;
	}
	for (;;) {
		uint32_t old = slot->address;
		if (__sync_bool_compare_and_swap(&slot->address, old, address)) {
			return old;
		}
	}
}

static uint32_t
_query(struct remote_objects *r, int handle) {
	struct remote_address * slot = r->addr + handle % MAX_REMOTE_OBJECT;
	if (slot->handle == handle) {
		return slot->address;
	}
	return 0;
}

static uint32_t
_getaddr(lua_State *L, int index) {
	size_t sz;
	const char * addr = luaL_checklstring(L,index,&sz);
	if (addr[0] != ':') {
		luaL_error(L, "Invalid address %s",addr);
	}
	return strtoul(addr+1, NULL, 16);
}

static void
_id_to_hex(char * str, uint32_t id) {
	int i;
	static char hex[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
	str[0] = ':';
	for (i=0;i<8;i++) {
		str[i+1] = hex[(id >> ((7-i) * 4))&0xf];
	}
	str[9] = '\0';
}

static int
lbind(lua_State *L) {
	uint32_t addr = lua_tounsigned(L,lua_upvalueindex(1));
	int handle = luaL_checkinteger(L,1);
	uint32_t old = _bind(_R, handle, addr);
	if (old == 0) {
		luaL_error(L, "handle %d is not exist", handle);
	}
	char tmp[10];
	_id_to_hex(tmp, old);
	lua_pushlstring(L,tmp,9);
	return 1;
}

static int
lremove(lua_State *L) {
	uint32_t * addr = lua_touserdata(L,1);
	_remove(_R, *addr);
	return 0;
}

static int
lalloc(lua_State *L) {
	uint32_t addr = lua_tounsigned(L,lua_upvalueindex(1));
	int handle = _alloc(_R, addr);
	if (handle < 0) {
		return luaL_error(L, "Too many remote object");
	}
	lua_pushinteger(L,handle);
	return 1;
}

static int
lquery(lua_State *L) {
	int handle = luaL_checkinteger(L,1);
	uint32_t addr = _query(_R, handle);
	if (addr == 0) {
		return 0;
	}
	char tmp[10];
	_id_to_hex(tmp, addr);
	lua_pushlstring(L,tmp,9);
	return 1;
}

int
remoteobj_init(lua_State *L) {
	if (_R == NULL) {
		struct remote_objects * r = _create();
		if (!__sync_bool_compare_and_swap(&_R, NULL, r)) {
			free(r);
		}
	}

	uint32_t address = _getaddr(L, -1);
	lua_pushcfunction(L, lquery);
	lua_pushnumber(L, address);

	uint32_t * addr = lua_newuserdata(L, sizeof(*addr));
	*addr = address;
	lua_createtable(L, 0, 1);
	lua_pushcfunction(L, lremove);
	lua_setfield(L, -2, "__gc");
	lua_setmetatable(L,-2);

	lua_pushcclosure(L, lalloc, 2);
	lua_pushnumber(L, address);
	lua_pushcclosure(L, lbind, 1);

	return 3;
}








