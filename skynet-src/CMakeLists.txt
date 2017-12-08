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

include_directories(
        ${PROJECT_SOURCE_DIR}/3rd/lua
        ${PROJECT_SOURCE_DIR}/skynet-src
        ${PROJECT_SOURCE_DIR}/3rd/jemalloc/include/jemalloc
)

add_executable(skynet ${SKYNET_SRC})

link_libraries(
        ${PROJECT_SOURCE_DIR}/3rd/lua/lib
        ${PROJECT_SOURCE_DIR}/3rd/jemalloc/lib
)

target_link_libraries(skynet lua )
target_link_libraries(skynet -ljemalloc )

add_definitions(-Wl,-E)
target_link_libraries(skynet -lpthread -lm -ldl -lrt)
