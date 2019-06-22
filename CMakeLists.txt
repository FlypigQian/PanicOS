cmake_minimum_required(VERSION 3.10)
project(panikos C ASM)

message(WARNING "CMake has not been supported yet.")

set(CMAKE_C_STANDARD 11)

include_directories(src)
include_directories(src/lib)
include_directories(src/lib/kernel)

file(GLOB_RECURSE SRCS "src/*.c")
add_library(kernel ${SRCS} src/vm/frame.h src/vm/frame.c src/vm/page.h src/vm/page.c src/vm/swap.h src/vm/swap.c)
target_compile_definitions(kernel PRIVATE USERPROG)

