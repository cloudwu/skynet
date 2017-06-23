#define LUA_LIB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

static unsigned int num_escape_sql_str(unsigned char *dst, unsigned char *src, size_t size)
{
    unsigned int n =0;
    while (size) {
        /* the highest bit of all the UTF-8 chars
         * is always 1 */
        if ((*src & 0x80) == 0) {
            switch (*src) {
                case '\0':
                case '\b':
                case '\n':
                case '\r':
                case '\t':
                case 26:  /* \Z */
                case '\\':
                case '\'':
                case '"':
                    n++;
                    break;
                default:
                    break;
            }
        }
        src++;
        size--;
    }
    return n;
}
static unsigned char*
escape_sql_str(unsigned char *dst, unsigned char *src, size_t size)
{
    
      while (size) {
        if ((*src & 0x80) == 0) {
            switch (*src) {
                case '\0':
                    *dst++ = '\\';
                    *dst++ = '0';
                    break;
                    
                case '\b':
                    *dst++ = '\\';
                    *dst++ = 'b';
                    break;
                    
                case '\n':
                    *dst++ = '\\';
                    *dst++ = 'n';
                    break;
                    
                case '\r':
                    *dst++ = '\\';
                    *dst++ = 'r';
                    break;
                    
                case '\t':
                    *dst++ = '\\';
                    *dst++ = 't';
                    break;
                    
                case 26:
                    *dst++ = '\\';
                    *dst++ = 'Z';
                    break;
                    
                case '\\':
                    *dst++ = '\\';
                    *dst++ = '\\';
                    break;
                    
                case '\'':
                    *dst++ = '\\';
                    *dst++ = '\'';
                    break;
                    
                case '"':
                    *dst++ = '\\';
                    *dst++ = '"';
                    break;
                    
                default:
                    *dst++ = *src;
                    break;
            }
        } else {
            *dst++ = *src;
        }
        src++;
        size--;
    } /* while (size) */
    
    return  dst;
}




static int
quote_sql_str(lua_State *L)
{
    size_t                   len, dlen, escape;
    unsigned char                  *p;
    unsigned char                  *src, *dst;
    
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "expecting one argument");
    }
    
    src = (unsigned char *) luaL_checklstring(L, 1, &len);
    
    if (len == 0) {
        dst = (unsigned char *) "''";
        dlen = sizeof("''") - 1;
        lua_pushlstring(L, (char *) dst, dlen);
        return 1;
    }
    
    escape = num_escape_sql_str(NULL, src, len);
    
    dlen = sizeof("''") - 1 + len + escape;
    p = lua_newuserdata(L, dlen);
    
    dst = p;
    
    *p++ = '\'';
    
    if (escape == 0) {
        memcpy(p, src, len);
        p+=len;
    } else {
        p = (unsigned char *) escape_sql_str(p, src, len);
    }
    
    *p++ = '\'';
    
    if (p != dst + dlen) {
        return luaL_error(L, "quote sql string error");
    }
    
    lua_pushlstring(L, (char *) dst, p - dst);
    
    return 1;
}


static struct luaL_Reg mysqlauxlib[] = {
    {"quote_sql_str",quote_sql_str},
    {NULL, NULL}
};


LUAMOD_API int luaopen_skynet_mysqlaux_c (lua_State *L) {
    lua_newtable(L);
    luaL_setfuncs(L, mysqlauxlib, 0);
    return 1;
}

