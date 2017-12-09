
include_directories(
        ${PROJECT_SOURCE_DIR}/lualib-src
        ${PROJECT_SOURCE_DIR}/lualib-src/sproto
        ${PROJECT_SOURCE_DIR}/service-src
        ${PROJECT_SOURCE_DIR}/skynet-src
        ${PROJECT_SOURCE_DIR}/3rd/lua
        ${PROJECT_SOURCE_DIR}/3rd/lua-md5
        ${PROJECT_SOURCE_DIR}/3rd/lpeg
)
add_definitions(-fPIC)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/luaclib)

#client.so
add_library(client SHARED
        lua-clientsocket.c
        lua-crypt.c
        lsha1.c)
target_link_libraries(client pthread)

#bson.so
add_library(bson SHARED lua-bson.c )


#md5.so
add_library(md5 SHARED
        ../3rd/lua-md5/md5.c
        ../3rd/lua-md5/md5lib.c
        ../3rd/lua-md5/compat-5.2.c
        )

#sproto.so
add_library(sproto SHARED
        sproto/sproto.c
        sproto/lsproto.c
        )


# lpeg.so
add_library(lpeg SHARED
        ../3rd/lpeg/lpcap.c
        ../3rd/lpeg/lpcode.c
        ../3rd/lpeg/lpprint.c
        ../3rd/lpeg/lptree.c
        ../3rd/lpeg/lpvm.c
        )

# myskynet.so
set( lualib_SRC
    lua-skynet.c
    lua-seri.c
    lua-socket.c
    lua-mongo.c
    lua-netpack.c
    lua-memory.c
    lua-profile.c
    lua-multicast.c
    lua-cluster.c
    lua-crypt.c
    lsha1.c
    lua-sharedata.c
    lua-stm.c
    lua-mysqlaux.c
    lua-debugchannel.c
    lua-datasheet.c
        )


add_library(skynet SHARED ${lualib_SRC} )