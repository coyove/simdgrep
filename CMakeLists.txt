cmake_minimum_required(VERSION 3.12)
project(simdgrep C)

set(CMAKE_C_STANDARD 11)

find_package(Threads REQUIRED)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Wall -Od -g)
else()
    add_compile_options(-Wall -O3)
endif()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    add_compile_options(-mavx2)
endif()

file(GLOB SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pcre2/deps/sljit/sljit_src/sljitLir.c"
)

set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_UNICODE ON CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)

add_subdirectory(pcre2)

add_executable(simdgrep ${SOURCES})

target_link_libraries(simdgrep PRIVATE Threads::Threads)
target_link_libraries(simdgrep PRIVATE pcre2-8)

target_include_directories(simdgrep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
