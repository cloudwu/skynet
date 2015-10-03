cmake_minimum_required(VERSION 2.6)
project(skynet)

find_package(Threads)

add_definitions(-DNOUSE_JEMALLOC)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "TinyCC")
message(STATUS "using pthread lock")
add_definitions(-DUSE_PTHREAD_LOCK)
endif()

include_directories(./3rd/lua)
include_directories(skynet-src)

add_subdirectory(./3rd/lua)
add_subdirectory(./3rd/lua-md5)
add_subdirectory(./3rd/lpeg)

set( SKYNET_SOURCES service-src/service_logger.c
service-src/service_snlua.c
service-src/service_harbor.c
service-src/service_gate.c

skynet-src/atomic.h         skynet-src/skynet_env.c     skynet-src/skynet_harbor.h  skynet-src/skynet_module.h   skynet-src/skynet_socket.c  skynet-src/socket_poll.h
skynet-src/luashrtbl.h      skynet-src/skynet_env.h     skynet-src/skynet_imp.h     skynet-src/skynet_monitor.c  skynet-src/skynet_socket.h  skynet-src/socket_server.c
skynet-src/malloc_hook.c    skynet-src/skynet_error.c   skynet-src/skynet_log.c     skynet-src/skynet_monitor.h  skynet-src/skynet_start.c   skynet-src/socket_server.h
skynet-src/malloc_hook.h    skynet-src/skynet.h         skynet-src/skynet_log.h     skynet-src/skynet_mq.c       skynet-src/skynet_timer.c   skynet-src/spinlock.h
skynet-src/rwlock.h         skynet-src/skynet_handle.c  skynet-src/skynet_main.c    skynet-src/skynet_mq.h       skynet-src/skynet_timer.h
skynet-src/skynet_daemon.c  skynet-src/skynet_handle.h  skynet-src/skynet_malloc.h  skynet-src/skynet_server.c   skynet-src/socket_epoll.h
skynet-src/skynet_daemon.h  skynet-src/skynet_harbor.c  skynet-src/skynet_module.c  skynet-src/skynet_server.h   skynet-src/socket_kqueue.h

)

add_library(lualib OBJECT

lualib-src/lsha1.c             lualib-src/lua-cluster.c       lualib-src/lua-memory.c     lualib-src/lua-mysqlaux.c  lualib-src/lua-seri.c       lualib-src/lua-skynet.c
lualib-src/lua-bson.c          lualib-src/lua-crypt.c         lualib-src/lua-mongo.c      lualib-src/lua-netpack.c   lualib-src/lua-seri.h       lualib-src/lua-socket.c
lualib-src/lua-clientsocket.c  lualib-src/lua-debugchannel.c  lualib-src/lua-multicast.c  lualib-src/lua-profile.c   lualib-src/lua-sharedata.c  lualib-src/lua-stm.c


lualib-src/sproto/lsproto.c  lualib-src/sproto/msvcint.h lualib-src/sproto/sproto.c  lualib-src/sproto/sproto.h

)

# 这些静态库的符号因为没有在主程序里引用, 故而直接作为静态库链接会导致 obj 被从可执行文件移除
# 所以需要使用 cmake OBJECT 库来组织引用. 将他们还是作为最终可执行文件的 obj 来链接, 而不是静态库
add_executable( skynet ${SKYNET_SOURCES} $<TARGET_OBJECTS:lualib> $<TARGET_OBJECTS:lua-md5> $<TARGET_OBJECTS:lpeg>)
# link to static libraries of skynet project
# target_link_libraries(skynet )

# link to bundled static libraries
target_link_libraries(skynet lua)

# link to standart library on UNIX systems
target_link_libraries(skynet dl m ${CMAKE_THREAD_LIBS_INIT})

