# Storm 2 for the PS Vita, headless (B4 of docs/PLANO_RUNTIME_VITA.md): the
# recompiled code (ARMv7) linked with the Vita runtime of vita/runtime (null
# GPU, silent real-time audio with XMA decoding, sceCtrl input, software MMU).
#
# Input: storm2/vita/generated/default (run `rexglue codegen storm2_vita_manifest.toml
# --ignore-stamp` from storm2/vita after a template change; derived from the
# game, never committed).
#
#   cmake -G Ninja -S vita/storm2 -B vita/storm2/build-vita ^
#     -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake [-DSTORM2_OPT=-O2]
#   cmake --build vita/storm2/build-vita
#
# Output: storm2_vita.vpk (title RXGT00006, extended memory: ATTRIBUTE2=12).
cmake_minimum_required(VERSION 3.21)
project(storm2_vita CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(REX_VITA_RUNTIME_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../runtime runtime)

set(STORM2_GEN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../storm2/vita/generated/default"
    CACHE PATH "Generated Storm 2 code for the Vita")
# Optimization level for the generated code (Release default of CMake is -O3).
set(STORM2_OPT "-O2" CACHE STRING "Optimization flags for the generated code")

file(GLOB STORM2_SOURCES ${STORM2_GEN_DIR}/*.cpp)
# storm2_register.cpp registers DLL modules with KernelState; Storm 2 is one
# module and its functions come from PPCImageConfig.func_mappings.
list(FILTER STORM2_SOURCES EXCLUDE REGEX "storm2_register\\.cpp$")
list(LENGTH STORM2_SOURCES STORM2_SOURCE_COUNT)
message(STATUS "Storm 2 generated sources: ${STORM2_SOURCE_COUNT}")

add_library(storm2_code STATIC ${STORM2_SOURCES})
target_include_directories(storm2_code PRIVATE ${STORM2_GEN_DIR})
# Plain guest loads/stores without volatile (MMIO-checked ones keep it): the
# compiler may then keep values in registers and drop repeated accesses.
option(STORM2_MEM_NONVOLATILE "Generated code: plain guest memory accesses not volatile" OFF)
if(STORM2_MEM_NONVOLATILE)
  target_compile_definitions(storm2_code PRIVATE REX_MEM_VOLATILE=)
endif()
# rex_vita_settings brings REXGLUE_SOFT_MMU, REX_GUEST_SOFT_MMU,
# REX_GUEST_SPLIT_U64 and the runtime headers.
target_link_libraries(storm2_code PRIVATE rex_vita_settings)
separate_arguments(STORM2_OPT_LIST UNIX_COMMAND "${STORM2_OPT}")
target_compile_options(storm2_code PRIVATE
  ${STORM2_OPT_LIST} -fwrapv
  -Wno-unused-parameter -Wno-unused-variable -Wno-unused-label -Wno-unused-but-set-variable)

# Native versions of hot guest functions (scripts/ppc_to_native.py), made from
# the generated code at build time into the build directory (derived from the
# game: never committed). Used by storm2/src/storm2_audio_native.cpp.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(STORM2_NATIVE_FUNCS sub_82599010)
set(STORM2_NATIVE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_native)
set(STORM2_NATIVE_INCS)
foreach(func ${STORM2_NATIVE_FUNCS})
  add_custom_command(
    OUTPUT ${STORM2_NATIVE_DIR}/${func}.inc
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_LIST_DIR}/../../scripts/ppc_to_native.py
            --gen-dir ${STORM2_GEN_DIR} --func ${func} --out ${STORM2_NATIVE_DIR}/${func}.inc
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../../scripts/ppc_to_native.py ${STORM2_GEN_DIR}/storm2_init.cpp
    COMMENT "Native version of ${func}")
  list(APPEND STORM2_NATIVE_INCS ${STORM2_NATIVE_DIR}/${func}.inc)
endforeach()

add_executable(storm2_vita src/main_vita.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../storm2/src/storm2_movies.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../storm2/src/storm2_audio_native.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../storm2/src/storm2_counter_wait.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../storm2/src/storm2_fence_wait.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../storm2/src/storm2_frame_probe.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../storm2/src/storm2_native_crt.cpp
  ${STORM2_NATIVE_INCS}
  ${CMAKE_CURRENT_LIST_DIR}/../common/vita_math_compat.c)
target_include_directories(storm2_vita PRIVATE ${STORM2_NATIVE_DIR})
# __sce_headroom (vitasdk armvita.x): free space between the code and data
# segments. vita-elf-create appends the module info and import tables (~3.6
# KiB here) after the code segment; the data segment only starts at the next
# 64 KiB boundary, so when the code ends close to it the conversion fails with
# "Cannot allocate N bytes for SCE data at end of segment" and then crashes
# (0xC0000374) on its unchecked error path. See docs/bugs.md.
target_link_options(storm2_vita PRIVATE -Wl,--gc-sections -Wl,--defsym,__sce_headroom=0x4000)
# Times of the GXM calls vitaGL makes (GxmCallReport in the GLES report).
target_link_options(storm2_vita PRIVATE
  -Wl,--wrap=sceGxmBeginScene -Wl,--wrap=sceGxmEndScene -Wl,--wrap=sceGxmDraw
  -Wl,--wrap=sceGxmNotificationWait -Wl,--wrap=sceGxmFinish -Wl,--wrap=sceGxmMidSceneFlush)
target_link_libraries(storm2_vita PRIVATE
  storm2_code rexgpu_gles_vita rexgpu_null_vita rexaudio_vita rexinput_vita rexruntime_vita
  rexcore_vita ${REX_VITA_GLES_LIBS} ${REX_VITA_SYSTEM_LIBS} ScePower_stub)

# STORM2_CONSOLE: build for the real PS Vita (smaller newlib heap and app
# defaults that leave the main RAM to the game; see main_vita.cpp). Same title
# id and data folder; its own VPK name.
option(STORM2_CONSOLE "Build for the PS Vita console instead of Vita3K" OFF)
if(STORM2_CONSOLE)
  target_compile_definitions(storm2_vita PRIVATE STORM2_CONSOLE=1)
  set(STORM2_VPK storm2_console.vpk)
  set(STORM2_VPK_NAME "Storm 2 (console)")
else()
  set(STORM2_VPK storm2_vita.vpk)
  set(STORM2_VPK_NAME "Storm 2 (headless)")
endif()

vita_create_self(eboot.bin storm2_vita)
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
vita_create_vpk(${STORM2_VPK} RXGT00006 eboot.bin
  VERSION "01.00"
  NAME "${STORM2_VPK_NAME}")
