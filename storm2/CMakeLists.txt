# storm2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(storm2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Native versions of hot guest functions (scripts/ppc_to_native.py), made from
# the generated code at build time into the build directory (derived from the
# game: never committed). Used by src/storm2_audio_native.cpp.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(STORM2_NATIVE_FUNCS sub_82599010 sub_827C3850)
set(STORM2_NATIVE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_native)
set(STORM2_NATIVE_INCS)
foreach(func ${STORM2_NATIVE_FUNCS})
    add_custom_command(
        OUTPUT ${STORM2_NATIVE_DIR}/${func}.inc
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_LIST_DIR}/../scripts/ppc_to_native.py
                --gen-dir ${CMAKE_CURRENT_LIST_DIR}/generated/default --func ${func}
                --out ${STORM2_NATIVE_DIR}/${func}.inc
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../scripts/ppc_to_native.py
                ${CMAKE_CURRENT_LIST_DIR}/generated/default/storm2_init.cpp
        COMMENT "Native version of ${func}")
    list(APPEND STORM2_NATIVE_INCS ${STORM2_NATIVE_DIR}/${func}.inc)
endforeach()

# Sources
set(STORM2_SOURCES
    src/main.cpp
    src/storm2_movies.cpp
    src/storm2_audio_native.cpp
    src/storm2_native_skin.cpp
    src/storm2_counter_wait.cpp
    src/storm2_fence_wait.cpp
    src/storm2_frame_probe.cpp
    src/storm2_job_wait.cpp
    src/storm2_native_crt.cpp
    src/storm2_texture_lookup.cpp
    ${STORM2_NATIVE_INCS}
)

if(WIN32)
    add_executable(storm2 WIN32 ${STORM2_SOURCES})
else()
    add_executable(storm2 ${STORM2_SOURCES})
endif()

target_include_directories(storm2 PRIVATE ${STORM2_NATIVE_DIR})
rexglue_setup_target(storm2)
