cmake_minimum_required(VERSION 3.10)
project(SeaBattle C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Поиск ZeroMQ
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# Добавляем исполняемые файлы
add_executable(server server.c common.c)
add_executable(client client.c common.c)

# Линковка ZeroMQ
target_include_directories(server PRIVATE ${ZMQ_INCLUDE_DIRS})
target_include_directories(client PRIVATE ${ZMQ_INCLUDE_DIRS})
target_link_libraries(server ${ZMQ_LIBRARIES})
target_link_libraries(client ${ZMQ_LIBRARIES})

# Флаги компиляции
target_compile_options(server PRIVATE ${ZMQ_CFLAGS_OTHER})
target_compile_options(client PRIVATE ${ZMQ_CFLAGS_OTHER})

