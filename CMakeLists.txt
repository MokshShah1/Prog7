cmake_minimum_required(VERSION 3.16)
project(hw7)

set(CMAKE_C_STANDARD 99)

# Add allocator library
add_library(tdmm STATIC tdmm.c)

# Include header
target_include_directories(tdmm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Build your benchmark executable (optional)
add_executable(hw7 main.c)
target_link_libraries(hw7 tdmm)

# Build the tester executable expected by Gradescope
add_executable(mftester mftester.c)   # You need to provide mftester.c from starter code
target_link_libraries(mftester tdmm)