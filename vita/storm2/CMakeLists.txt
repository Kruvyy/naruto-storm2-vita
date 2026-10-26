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
# rex_vita_settings brings REXGLUE_SOFT_MMU, REX_GUEST_SOFT_MMU,
# REX_GUEST_SPLIT_U64 and the runtime headers.
target_link_libraries(storm2_code PRIVATE rex_vita_settings)
separate_arguments(STORM2_OPT_LIST UNIX_COMMAND "${STORM2_OPT}")
target_compile_options(storm2_code PRIVATE
  ${STORM2_OPT_LIST} -fwrapv
  -Wno-unused-parameter -Wno-unused-variable -Wno-unused-label -Wno-unused-but-set-variable)

add_executable(storm2_vita src/main_vita.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../common/vita_math_compat.c)
target_link_options(storm2_vita PRIVATE -Wl,--gc-sections)
target_link_libraries(storm2_vita PRIVATE
  storm2_code rexgpu_gles_vita rexgpu_null_vita rexaudio_vita rexinput_vita rexruntime_vita
  rexcore_vita ${REX_VITA_GLES_LIBS} ${REX_VITA_SYSTEM_LIBS})

vita_create_self(eboot.bin storm2_vita)
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
vita_create_vpk(storm2_vita.vpk RXGT00006 eboot.bin
  VERSION "01.00"
  NAME "Storm 2 (headless)")
