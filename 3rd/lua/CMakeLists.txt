

set(lua_SRCS
        lapi.c
        lauxlib.c
        lbaselib.c
        lbitlib.c
        lcode.c
        lcorolib.c
        lctype.c
        ldblib.c
        ldebug.c
        ldo.c
        ldump.c
        lfunc.c
        lgc.c
        linit.c
        liolib.c
        llex.c
        lmathlib.c
        lmem.c
        loadlib.c
        lobject.c
        lopcodes.c
        loslib.c
        lparser.c
        lstate.c
        lstring.c
        lstrlib.c
        ltable.c
        ltablib.c
        ltm.c
        lua.c
        luac.c
        lundump.c
        lutf8lib.c
        lvm.c
        lzio.c
        )
include_directories(./../../skynet-src)
add_library(lua ${lua_SRCS})
target_link_libraries(lua m)