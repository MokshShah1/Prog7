cmake_minimum_required(VERSION 3.16)
project(hw7 C)

set(CMAKE_C_STANDARD 99)

# Add the TDMM library (let libtmm handle its own sources)
add_subdirectory(libtmm)

# Build main.c as the test executable (optional)
add_executable(hw7 main.c)
target_link_libraries(hw7 tdmm)