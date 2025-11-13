cmake_minimum_required(VERSION 3.10)

project(lab_02_osi)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Wextra -pedantic")

add_executable(lab_02_osi_app main.c)
