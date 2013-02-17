cmake_minimum_required(VERSION 2.6)

add_library(http_parser http_parser.c)

add_executable(rsva11001adapter main.c rsva11001.c)

target_link_libraries(rsva11001adapter http_parser)
