/*
** $Id: lzio.h,v 1.31 2015/09/08 15:41:05 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "lua.h"

#include "lmem.h"


#define EOZ	(-1)			/* end of stream */

typedef struct Zio ZIO;

#define zgetc(z)  (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : luaZ_fill(z))


typedef struct Mbuffer {
  char *buffer;
  size_t n;
  size_t buffsize;
} Mbuffer;

#define luaZ_initbuffer(L, buff) ((buff)->buffer = NULL, (buff)->buffsize = 0)

#define luaZ_buffer(buff)	((buff)->buffer)
#define luaZ_sizebuffer(buff)	((buff)->buffsize)
#define luaZ_bufflen(buff)	((buff)->n)

#define luaZ_buffremove(buff,i)	((buff)->n -= (i))
#define luaZ_resetbuffer(buff) ((buff)->n = 0)


#define luaZ_resizebuffer(L, buff, size) \
	((buff)->buffer = luaM_reallocvchar(L, (buff)->buffer, \
				(buff)->buffsize, size), \
	(buff)->buffsize = size)

#define luaZ_freebuffer(L, buff)	luaZ_resizebuffer(L, buff, 0)


LUAI_FUNC void luaZ_init (lua_State *L, ZIO *z, lua_Reader reader,
                                        void *data);
LUAI_FUNC size_t luaZ_read (ZIO* z, void *b, size_t n);	/* read next n bytes */



/* --------- Private Part ------------------ */

struct Zio {
  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  lua_Reader reader;		/* reader function */
  void *data;			/* additional data */
  lua_State *L;			/* Lua state (for reader) */
};


LUAI_FUNC int luaZ_fill (ZIO *z);

#endif
