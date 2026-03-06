cmake_minimum_required(VERSION 3.16)
project(hw7 C)

set(CMAKE_C_STANDARD 99)

add_library(tdmm STATIC tdmm.c)

add_executable(hw7 main.c)
target_link_libraries(hw7 tdmm)