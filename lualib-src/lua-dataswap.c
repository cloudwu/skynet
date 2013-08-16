/* lua-dataswap.c --- 
 * 
 * Copyright (c) 2013 chale.suu@gmail.com 
 * 
 * Author:  lalawue
 * Maintainer: 
 * 
 * Created: 2013/08/15 12:24
 * Last-Updated: 2013/08/15 23:50
 * 
 */

/* Commentary: 
 * 
 * 
 */

/* Code: */

#include <lua.h>
#include <lauxlib.h>
#include "luacompat52.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "lua-dataswap.h"

static int
_op_swap(lua_State *L) {
    void *msg = lua_touserdata(L, 1);
    /* size_t sz = luaL_checkinteger(L, 2); */

    struct s_mydata *data = (struct s_mydata*)msg;

    printf("mydata recv %d %d %d %d\n", data->a, data->b, data->c, data->d);

    int tmp = data->a;
    data->a = data->d;
    data->d = tmp;

    tmp = data->b;
    data->b = data->c;
    data->c = tmp;

    lua_pushlightuserdata(L, data);
    lua_pushinteger(L, sizeof(*data));

    return 2;
}


int
luaopen_dataswap_c(lua_State *L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
        { "swap", _op_swap },
        { NULL, NULL },
    };

    luaL_newlib(L, l);
    return 1;
}

/* lua-dataswap.c ends here */
