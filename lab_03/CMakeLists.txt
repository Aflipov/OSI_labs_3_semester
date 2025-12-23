cmake_minimum_required(VERSION 3.10)

project(lab_03_osi)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Wextra -pedantic")

add_executable(lab_03_osi_app main.c shared_block_t.h)
add_executable(child child.c shared_block_t.h)

target_link_libraries(lab_03_osi_app PRIVATE pthread)
target_link_libraries(child PRIVATE pthread)
