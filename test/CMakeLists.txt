cmake_minimum_required (VERSION 2.6)
include_directories ("${PROJECT_SOURCE_DIR}/src")

add_executable(http_parser_test http_parser_test.c)

target_link_libraries(http_parser_test http_parser)

ADD_TEST(http_parser_test ${CMAKE_CURRENT_BINARY_DIR}/http_parser_test)

