cmake_minimum_required(VERSION 3.10)

project(lab_02_osi)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_RELEASE "-O3")

find_package(Threads REQUIRED)

add_executable(gauss_solver gauss_solver.c)

target_link_libraries(gauss_solver PRIVATE Threads::Threads)
