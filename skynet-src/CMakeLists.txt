set(SKYNET_SRC
        skynet_main.c
        skynet_handle.c
        skynet_module.c
        skynet_mq.c
        skynet_server.c
        skynet_start.c
        skynet_timer.c
        skynet_error.c
        skynet_harbor.c
        skynet_env.c
        skynet_monitor.c
        skynet_socket.c
        socket_server.c
        malloc_hook.c
        skynet_daemon.c
        skynet_log.c
        )
set(CMAKE_VERBOSE_MAKEFILE ON)#show compile command in console

set(TARGET_EXE myskynet)
set(EXECUTABLE_OUTPUT_PATH  ${PROJECT_SOURCE_DIR})#set executable output path

include_directories(
        ${PROJECT_SOURCE_DIR}/3rd/lua
        ${PROJECT_SOURCE_DIR}/skynet-src
        ${PROJECT_SOURCE_DIR}/3rd/jemalloc/include/jemalloc
)

#add_definitions(-Wl,-E)  #using add_definitions can't add "-WL,-E" ，it will cause that dlopen() return undefined symbol
set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -Wl,-E" )

find_library(JEMALLOC_LIB jemalloc_pic ../3rd/jemalloc/lib)
IF(NOT JEMALLOC_LIB)
    message(FATAL_ERROR "jemalloc_pic not found")
ENDIF(NOT JEMALLOC_LIB)
message(STATUS "jemalloc_pic found in " ${JEMALLOC_LIB})


add_executable(${TARGET_EXE} ${SKYNET_SRC})


link_directories(
        ${PROJECT_SOURCE_DIR}/3rd/lua
#        ${PROJECT_SOURCE_DIR}/3rd/jemalloc/lib
)

target_link_libraries(${TARGET_EXE} lua ${JEMALLOC_LIB})

target_link_libraries(${TARGET_EXE} pthread m dl rt)
