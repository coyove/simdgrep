cmake_minimum_required(VERSION 3.12)
project(simdgrep C)

set(CMAKE_C_STANDARD 11)

include(ExternalProject)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Wall -Od -g)
else()
    add_compile_options(-O3)
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

set(externals_prefix "${CMAKE_CURRENT_BINARY_DIR}/externals")
set(externals_install_dir "${CMAKE_CURRENT_BINARY_DIR}/libs")

set_property(DIRECTORY PROPERTY EP_BASE ${externals_prefix})
ExternalProject_Add(lpthread
    GIT_REPOSITORY "https://github.com/Puasonych/pthread-win.git"
    GIT_TAG "master"
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${externals_install_dir} -DPTHREAD_BUILD_SHARED=OFF
)

include_directories(${externals_install_dir}/include/pthread)
set(PTHREAD_LIB "${externals_install_dir}/lib/libpthread.lib")

add_subdirectory(pcre2)

add_executable(simdgrep ${SOURCES})
add_dependencies(simdgrep lpthread)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /experimental:c11atomics")

target_link_libraries(simdgrep PRIVATE pcre2-8)

target_include_directories(simdgrep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
