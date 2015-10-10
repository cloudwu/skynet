cmake_minimum_required(VERSION 2.6)
project(lua)


set(LUA_SOURCES

lapi.c     lbaselib.c  lcorolib.c  ldebug.c  ldump.c  lgc.h     llex.h      lmem.h     lopcodes.c  lparser.h  lstring.c  ltable.h   lua.c      lualib.h    lvm.c
lapi.h     lbitlib.c   lctype.c    ldebug.h  lfunc.c  linit.c   llimits.h   loadlib.c  lopcodes.h  lprefix.h  lstring.h  ltablib.c  luac.c     lundump.c   lvm.h
lauxlib.c  lcode.c     lctype.h    ldo.c     lfunc.h  liolib.c  lmathlib.c  lobject.c  loslib.c    lstate.c   lstrlib.c  ltm.c      luaconf.h  lundump.h   lzio.c
lauxlib.h  lcode.h     ldblib.c    ldo.h     lgc.c    llex.c    lmem.c      lobject.h  lparser.c   lstate.h   ltable.c   ltm.h      lua.h      lutf8lib.c  lzio.h

)

include_directories(../../skynet-src)

add_library( lua STATIC ${LUA_SOURCES})


