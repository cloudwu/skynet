cmake_minimum_required(VERSION 3.8)
project(skynet)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS  "-std=gnu99 -g -O2 -Wall -march=x86-64" )

add_subdirectory(${PROJECT_SOURCE_DIR}/3rd/lua )

#add_subdirectory(${PROJECT_SOURCE_DIR}/3rd/jemalloc)

add_subdirectory(${PROJECT_SOURCE_DIR}/service-src)

add_subdirectory(${PROJECT_SOURCE_DIR}/skynet-src )


