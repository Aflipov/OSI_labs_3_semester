cmake_minimum_required(VERSION 3.10)
project(lab_04 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(lib1 SHARED lib1.c)
set_target_properties(lib1 PROPERTIES OUTPUT_NAME "lib1" PREFIX "")
target_link_libraries(lib1 m) # m <= math.h

add_library(lib2 SHARED lib2.c)
set_target_properties(lib2 PROPERTIES OUTPUT_NAME "lib2" PREFIX "")

add_executable(program1 program1.c)
target_link_libraries(program1 lib1 m)

add_executable(program2 program2.c)
target_link_libraries(program2 dl) # dl <= dlfcn.h
