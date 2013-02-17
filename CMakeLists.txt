cmake_minimum_required(VERSION 2.6)
ENABLE_TESTING()
set(CMAKE_C_FLAGS "-std=gnu99")
project(rsva11001adapter)
set(rsva11001adapter_VERSION_MAJOR 0)
set(rsva11001adapter_VERSION_MINOR 1)

add_subdirectory(src)
add_subdirectory(test)
