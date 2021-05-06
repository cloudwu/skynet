#include <stdio.h>
#include <assert.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"

inline static int
_steps(uint8_t c) {
    if(c < 0x80) return 1;
    if(c < 0xc0) return 0;
    if(c < 0xe0) return 2;
    if(c < 0xf0) return 3;
    if(c < 0xf8) return 4;
    return 0;
}

inline static int
_bytes(uint32_t rune) {
    if(rune < 0x80) return 1;
    if(rune < 0x800) return 2;
    if(rune < 0x10000) return 3;
    if(rune < 0x110000) return 4;
    return 0;
}

inline static uint32_t
_decode(const char *str, int i, int step) {
    uint8_t c = str[i];
    uint32_t v = c & (0xff >> step);
    int j = 1;
    for(;j<step; j++) {
        v = v << 6;
        v = v | (str[i+j] & 0x3f);
    }
    return v;
}

#define FILL_LOW_BITS(str, pos, rune) str[pos] = (rune & 0x3f) | 0x80; rune >>= 6;

inline static uint8_t*
_encode(uint32_t rune, int bytes, uint8_t* str) {
    if (bytes == 1) {
        str[0] = rune & 0x7f;
    } else if(bytes == 2) {
        FILL_LOW_BITS(str, 1, rune)
        str[0] = rune | 0xc0;
    } else if(bytes == 3) {
        FILL_LOW_BITS(str, 2, rune)
        FILL_LOW_BITS(str, 1, rune)
        str[0] = rune | 0xe0;
    } else {
        FILL_LOW_BITS(str, 3, rune)
        FILL_LOW_BITS(str, 2, rune)
        FILL_LOW_BITS(str, 1, rune)
        str[0] = rune | 0xf0;
    }
    return str + bytes;
}

static int
_toutf32(lua_State *L) {
    size_t len;
    const char* str = luaL_checklstring(L, 1, &len);
    luaL_checktype(L, 2, LUA_TTABLE);

    int count = 0;

    int i, step;
    uint8_t c;
    for(i=0;i<len;) {
        c = str[i];
        step = _steps(c);
        if(step == 0 || len < i + step) {
            count = -1;
            break;
        }
        lua_pushinteger(L, _decode(str, i, step));
        count = count + 1;
        lua_rawseti(L, 2, count);

        i = i + step;
    }

    if(count < 0) {
        return 0;
    }
    lua_pushinteger(L, count);
    return 1;
}

static int
_toutf8(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    size_t sz = 0;
    size_t len = lua_rawlen(L, 1);
    size_t i;
    for(i = 1; i <= len; i++) {
        lua_rawgeti(L, 1, i);
        int isnum;
        uint32_t rune = (uint32_t)lua_tointegerx(L, -1, &isnum);
        lua_pop(L, 1);

        if(!isnum) {
            return 0;
        }
        int bytes = _bytes(rune);
        if(!bytes) {
            return 0;
        }
        sz += bytes;
    }

    uint8_t *str = lua_newuserdata(L, sz);
    uint8_t *tmp = str;
    for(i = 1; i <= len; i++) {
        lua_rawgeti(L, 1, i);
        uint32_t rune = lua_tointeger(L, -1);
        lua_pop(L, 1);

        int bytes = _bytes(rune);
        tmp = _encode(rune, bytes, tmp);
    }

    lua_pushlstring(L, (char*)str, sz);
    return 1;
}

static int
_len(lua_State *L) {
    size_t len;
    const char* str = luaL_checklstring(L, 1, &len);

    int count = 0;
    int i, step;
    uint8_t c;
    for(i=0;i<len;) {
        c = str[i];
        step = _steps(c);
        i = i + step;
        if(!step || len < i) {
            count = -1;
            break;
        }
        count = count + 1;
    }
    if(count < 0) {
        return 0;
    }
    lua_pushinteger(L, count);
    return 1;
}

int
luaopen_utf8_c(lua_State *L) {
    luaL_checkversion(L);

    luaL_Reg l[] = {
        {"len", _len},
        {"toutf32", _toutf32},
        {"toutf8", _toutf8},
        {NULL, NULL}
    };

    luaL_newlib(L, l);
    return 1;
}

