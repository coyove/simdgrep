cmake_minimum_required(VERSION 3.12)

project(simdgrep C)

set(CMAKE_C_STANDARD 11)

include(ExternalProject)
include(FetchContent)

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

if(WIN32)
    set(externals_prefix "${CMAKE_CURRENT_BINARY_DIR}/externals")
    set(externals_install_dir "${CMAKE_CURRENT_BINARY_DIR}/libs")
    set_property(DIRECTORY PROPERTY EP_BASE ${externals_prefix})

    ExternalProject_Add(lpthread
        GIT_REPOSITORY "https://github.com/Puasonych/pthread-win.git"
        GIT_TAG "master"
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${externals_install_dir} -DPTHREAD_BUILD_SHARED=OFF -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    )
    include_directories(${externals_install_dir}/include/pthread)
    set(PTHREAD_LIB "${externals_install_dir}/lib/pthread.lib")
    add_dependencies(simdgrep lpthread)
    add_definitions(-DPTW32_STATIC_LIB)
    target_link_libraries(simdgrep PRIVATE ${PTHREAD_LIB})

    add_subdirectory(dirent)
    target_link_libraries(simdgrep PRIVATE dirent)

    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /experimental:c11atomics")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /experimental:c11atomics")
    set_property(TARGET simdgrep PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(simdgrep PRIVATE Threads::Threads)
endif()

target_link_libraries(simdgrep PRIVATE pcre2-8)
target_include_directories(simdgrep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
