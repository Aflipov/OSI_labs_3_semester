cmake_minimum_required(VERSION 3.10)

project(lab_01_osi)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Wextra -pedantic")

add_executable(lab_01_osi_app parent.c)

add_executable(child child.c)
