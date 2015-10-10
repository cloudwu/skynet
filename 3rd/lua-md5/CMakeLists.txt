
set(LUAMD5_SOURCES

compat-5.2.c  compat-5.2.h  md5.c  md5.h  md5lib.c

)

include_directories(../../skynet-src)

add_library( lua-md5 OBJECT ${LUAMD5_SOURCES})


