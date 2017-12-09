cmake_minimum_required(VERSION 3.8)
project(skynet)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS  " -O2 -Wall" )


set(CMAKE_SHARED_LIBRARY_PREFIX "")


# lua.so
add_subdirectory(${PROJECT_SOURCE_DIR}/3rd/lua )

#add_subdirectory(${PROJECT_SOURCE_DIR}/3rd/jemalloc)

# gate.so harbor.so logger.so snlua.so
add_subdirectory(${PROJECT_SOURCE_DIR}/service-src)

# executabale myskynet
add_subdirectory(${PROJECT_SOURCE_DIR}/skynet-src )

# client.so  bson.so  md5.so  sproto.so  lpeg.so  skynet.so
add_subdirectory(${PROJECT_SOURCE_DIR}/lualib-src )

