/*
 * luaiconv - Performs character set conversions in Lua
 * (c) 2005-11 Alexandre Erwin Ittner <alexandre@ittner.com.br>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * If you use this package in a product, an acknowledgment in the product
 * documentation would be greatly appreciated (but it is not required).
 *
 *
 */


#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>

#include <iconv.h>
#include <errno.h>

#define LIB_NAME                "iconv"
#define LIB_VERSION             LIB_NAME " 7"
#define ICONV_TYPENAME          "iconv_t"

#if LUA_VERSION_NUM < 501
 #error "Unsupported Lua version. You must use Lua >= 5.1"
#endif

#if LUA_VERSION_NUM < 502
 #define luaL_newlib(L, f)  { lua_newtable(L); luaL_register(L, NULL, f); }
 #define lua_rawlen(L, i)   lua_objlen(L, i)
#endif


#define BOXPTR(L, p)   (*(void**)(lua_newuserdata(L, sizeof(void*))) = (p))
#define UNBOXPTR(L, i) (*(void**)(lua_touserdata(L, i)))

/* Set a integer constant. Assumes a table in the top of the stack */
#define TBL_SET_INT_CONST(L, c) {   \
    lua_pushliteral(L, #c);         \
    lua_pushnumber(L, c);           \
    lua_settable(L, -3);            \
}

#define ERROR_NO_MEMORY     1
#define ERROR_INVALID       2
#define ERROR_INCOMPLETE    3
#define ERROR_UNKNOWN       4
#define ERROR_FINALIZED     5



static void push_iconv_t(lua_State *L, iconv_t cd)
{
    BOXPTR(L, cd);
    luaL_getmetatable(L, ICONV_TYPENAME);
    lua_setmetatable(L, -2);
}


static iconv_t get_iconv_t(lua_State *L, int i)
{
    if (luaL_checkudata(L, i, ICONV_TYPENAME) != NULL) {
        iconv_t cd = UNBOXPTR(L, i);
        return cd;  /* May be NULL. This must be checked by the caller. */
    }
    luaL_argerror(L, i, lua_pushfstring(L, ICONV_TYPENAME " expected, got %s",
        luaL_typename(L, i)));
    return NULL;
}


static int Liconv_open(lua_State *L)
{
    const char *tocode = luaL_checkstring(L, 1);
    const char *fromcode = luaL_checkstring(L, 2);
    iconv_t cd = iconv_open(tocode, fromcode);
    if (cd != (iconv_t)(-1))
        push_iconv_t(L, cd);    /* ok */
    else
        lua_pushnil(L);         /* error */
    return 1;
}

/* Use a fixed-size buffer in the stack to avoid a lot of small mallocs
 * and prevent memory fragmentation. This should not be a problem in any
 * contemporary general purpose system but, if you are running in a very
 * limited stack system you may use a smaller buffer, but the luaL_Buffer
 * will compensate this with more reallocs and memcpys.
 */
#define CONV_BUF_SIZE 256

static int Liconv(lua_State *L)
{
    iconv_t cd = get_iconv_t(L, 1);
    size_t ibleft = lua_rawlen(L, 2);
    char *inbuf = (char*) luaL_checkstring(L, 2);
    char outbufs[CONV_BUF_SIZE];
    char *outbuf = outbufs;
    size_t obleft = CONV_BUF_SIZE;
    size_t ret = -1;

    if (cd == NULL) {
        lua_pushstring(L, "");
        lua_pushnumber(L, ERROR_FINALIZED);
        return 2;
    }

    luaL_Buffer b;
    luaL_buffinit(L, &b);

    do {
        ret = iconv(cd, &inbuf, &ibleft, &outbuf, &obleft);
        if (ret == (size_t)(-1)) {
            luaL_addlstring(&b, outbufs, CONV_BUF_SIZE - obleft);
            if (errno == EILSEQ) {
                luaL_pushresult(&b);
                lua_pushnumber(L, ERROR_INVALID);
                return 2;   /* Invalid character sequence */
            } else if (errno == EINVAL) {
                luaL_pushresult(&b);
                lua_pushnumber(L, ERROR_INCOMPLETE);
                return 2;   /* Incomplete character sequence */
            } else if (errno == E2BIG) {
                obleft = CONV_BUF_SIZE;
                outbuf = outbufs;
            } else {
                luaL_pushresult(&b);
                lua_pushnumber(L, ERROR_UNKNOWN);
                return 2; /* Unknown error */
            }
        }
    } while (ret == (size_t) -1);

    luaL_addlstring(&b, outbufs, CONV_BUF_SIZE - obleft);
    luaL_pushresult(&b);
    lua_pushnil(L);
    return 2;   /* Done */
}



#ifdef HAS_ICONVLIST /* a GNU extension? */

static int push_one(unsigned int cnt, char *names[], void *data)
{
    lua_State *L = (lua_State*) data;
    int n = (int) lua_tonumber(L, -1);
    int i;

    /* Stack: <tbl> n */
    lua_remove(L, -1);    
    for (i = 0; i < cnt; i++) {
        /* Stack> <tbl> */
        lua_pushnumber(L, n++);
        lua_pushstring(L, names[i]);
        /* Stack: <tbl> n <str> */
        lua_settable(L, -3);
    }
    lua_pushnumber(L, n);
    /* Stack: <tbl> n */
    return 0;   
}


static int Liconvlist(lua_State *L)
{
    lua_newtable(L);
    lua_pushnumber(L, 1);

    /* Stack:   <tbl> 1 */
    iconvlist(push_one, (void*) L);

    /* Stack:   <tbl> n */
    lua_remove(L, -1);
    return 1;
}

#endif


static int Liconv_close(lua_State *L)
{
    iconv_t cd = get_iconv_t(L, 1);
    if (cd != NULL && iconv_close(cd) == 0) {
        /* Mark the pointer as freed, preventing interpreter crashes
           if the user forces __gc to be called twice. */
        void **ptr = lua_touserdata(L, 1);
        *ptr = NULL;
        lua_pushboolean(L, 1);  /* ok */
    }
    else
        lua_pushnil(L);         /* error */
    return 1;
}


static const luaL_Reg iconv_funcs[] = {
    { "open",   Liconv_open },
    { "new",    Liconv_open },
    { "iconv",  Liconv },
#ifdef HAS_ICONVLIST
    { "list",   Liconvlist },
#endif
    { NULL, NULL }
};


int luaopen_iconv(lua_State *L)
{
    luaL_newlib(L, iconv_funcs);

    TBL_SET_INT_CONST(L, ERROR_NO_MEMORY);
    TBL_SET_INT_CONST(L, ERROR_INVALID);
    TBL_SET_INT_CONST(L, ERROR_INCOMPLETE);
    TBL_SET_INT_CONST(L, ERROR_FINALIZED);
    TBL_SET_INT_CONST(L, ERROR_UNKNOWN);

    lua_pushliteral(L, "VERSION");
    lua_pushstring(L, LIB_VERSION);
    lua_settable(L, -3);

    luaL_newmetatable(L, ICONV_TYPENAME);

    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);
    lua_settable(L, -3);

    lua_pushliteral(L, "__gc");
    lua_pushcfunction(L, Liconv_close);
    lua_settable(L, -3);

    lua_pop(L, 1);

    return 1;
}
