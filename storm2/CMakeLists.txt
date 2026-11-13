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
set(STORM2_NATIVE_FUNCS sub_82599010 sub_827C3850 sub_82654128 sub_825DD778 sub_82638CB8)
# sub_82654128 (bone matrices and per-vertex transforms on the main thread,
# storm2_native_mesh.cpp): its small vector leaves inlined, the stack frame in
# guest memory, registers kept in step with PPCContext (virtual calls).
set(STORM2_NATIVE_ARGS_sub_82654128
    --inline sub_825CA318,sub_825CA820,sub_827B2400,sub_825CA2F8,sub_825C9A38,sub_825CA768,sub_825C9F20
    --guest-stack --ctx-regs --track-stores)
# sub_825DD778 (bone matrices and their inverses, storm2_native_inverse.cpp):
# the 3x3 inverse sub_825C9F20 inlined.
set(STORM2_NATIVE_ARGS_sub_825DD778 --inline sub_825C9F20 --guest-stack --ctx-regs)
# sub_82638CB8 (scene node world matrices, storm2_native_scene.cpp).
set(STORM2_NATIVE_ARGS_sub_82638CB8
    --inline sub_825C9A38,sub_825CA2F8,sub_825CA318,sub_825C9A10
    --guest-stack --ctx-regs --track-stores)
set(STORM2_NATIVE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_native)
set(STORM2_NATIVE_INCS)
foreach(func ${STORM2_NATIVE_FUNCS})
    add_custom_command(
        OUTPUT ${STORM2_NATIVE_DIR}/${func}.inc
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_LIST_DIR}/../scripts/ppc_to_native.py
                --gen-dir ${CMAKE_CURRENT_LIST_DIR}/generated/default --func ${func} ${STORM2_NATIVE_ARGS_${func}}
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
    src/storm2_native_mesh.cpp
    src/storm2_native_inverse.cpp
    src/storm2_native_scene.cpp
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

# storm2_native_mesh.cpp includes the generated storm2_pch.h (indirect calls).
target_include_directories(storm2 PRIVATE ${STORM2_NATIVE_DIR} ${CMAKE_CURRENT_LIST_DIR}/generated/default)
rexglue_setup_target(storm2)
