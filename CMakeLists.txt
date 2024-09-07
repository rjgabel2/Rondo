cmake_minimum_required(VERSION 3.20)
project(Rondo)

add_executable(Rondo
src/cpu.c
src/gb.c
src/ldc.c
src/main.c
)

target_include_directories(Rondo PRIVATE include SDL2)
target_link_libraries(Rondo ${CMAKE_CURRENT_SOURCE_DIR}/SDL2.dll)

target_compile_options(Rondo PRIVATE -Wall -Wextra)