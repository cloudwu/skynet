
include_directories(
        ${PROJECT_SOURCE_DIR}/skynet-src
        ${PROJECT_SOURCE_DIR}/3rd/lua
)
#
add_definitions(-fPIC)
add_library(snlua SHARED service_snlua.c)

add_library(logger SHARED service_logger.c)

add_library(gate SHARED service_gate.c)

add_library(habor SHARED service_harbor.c)

