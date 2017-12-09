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

set(TARGET_EXE skynet)
set(EXECUTABLE_OUTPUT_PATH  ${PROJECT_SOURCE_DIR})#设置可执行文件的生成路径

include_directories(
        ${PROJECT_SOURCE_DIR}/3rd/lua
        ${PROJECT_SOURCE_DIR}/skynet-src
        ${PROJECT_SOURCE_DIR}/3rd/jemalloc/include/jemalloc
)

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

add_definitions(-Wl,-E)
target_link_libraries(${TARGET_EXE} -lpthread -lm -ldl -lrt)
