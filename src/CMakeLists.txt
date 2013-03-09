
cmake_minimum_required(VERSION 2.6)
set(CMAKE_C_FLAGS "-std=gnu99")

#add_definitions(-pg)
find_library(SANDGROUSE_LIB NAMES sandgrouse)
find_path(SANDGROUSE_INCLUDE_DIR sandgrouse/server.h)
include_directories(${SANDGROUSE_INCLUDE_DIR})
add_library(http_parser http_parser.c)
add_executable(rsva11001adapter main.c rsva11001.c)

target_link_libraries(rsva11001adapter http_parser ${SANDGROUSE_LIB})
