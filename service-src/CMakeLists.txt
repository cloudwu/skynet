
include_directories(
        ${PROJECT_SOURCE_DIR}/skynet-src
        ${PROJECT_SOURCE_DIR}/3rd/lua
)
#
add_library(snlua service_snlua.c)
#
#link_libraries(
#        ${PROJECT_SOURCE_DIR}/3rd/lua/lib
#        ${PROJECT_SOURCE_DIR}/3rd/jemalloc/lib
#)
#
#
#add_definitions("-E")
#target_link_libraries(skynet pthread dl rt)
