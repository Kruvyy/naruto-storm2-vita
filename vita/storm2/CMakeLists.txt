# Storm 2 recompiled code for the PS Vita (ARMv7). Step 1 of Marco E:
# compile every generated translation unit with the VitaSDK toolchain into a
# static library, to find compile errors and measure code size before a Vita
# runtime exists (no link yet: kernel/XAM imports, GPU and audio are missing).
#
# Input: storm2/generated_vita/default (rexglue codegen storm2_vita_manifest.toml;
# derived from the game, never committed).
#
#   cmake -G Ninja -S vita/storm2 -B vita/storm2/build-vita \
#     -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake [-DSTORM2_OPT=-O2]
#   cmake --build vita/storm2/build-vita
cmake_minimum_required(VERSION 3.20)
project(storm2_vita CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(REXGLUE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../rexglue" CACHE PATH "ReXGlue checkout")
set(STORM2_GEN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../storm2/generated_vita/default"
    CACHE PATH "Generated Storm 2 code for the Vita")
# Optimization level for the generated code (Release default of CMake is -O3).
set(STORM2_OPT "-O2" CACHE STRING "Optimization flags for the generated code")

file(GLOB STORM2_SOURCES ${STORM2_GEN_DIR}/*.cpp)
# storm2_register.cpp is desktop-runtime glue (registers the module with
# KernelState, includes rex/system/xmemory.h); the Vita runtime will provide
# its own. Everything else is guest code or its tables.
list(FILTER STORM2_SOURCES EXCLUDE REGEX "storm2_register\\.cpp$")
list(LENGTH STORM2_SOURCES STORM2_SOURCE_COUNT)
message(STATUS "Storm 2 generated sources: ${STORM2_SOURCE_COUNT}")

add_library(storm2_code STATIC ${STORM2_SOURCES})
# Runtime-facing headers come from the test-harness shims for now (logging,
# clock, MMIO, global lock); the Vita runtime will replace them.
target_include_directories(storm2_code BEFORE PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../ppc_tests/shim
  ${STORM2_GEN_DIR}
  ${REXGLUE_DIR}/include
  ${REXGLUE_DIR}/thirdparty/simde)
target_compile_definitions(storm2_code PRIVATE REX_GUEST_SOFT_MMU REX_GUEST_SPLIT_U64)
separate_arguments(STORM2_OPT_LIST UNIX_COMMAND "${STORM2_OPT}")
target_compile_options(storm2_code PRIVATE
  ${STORM2_OPT_LIST} -fwrapv -ffunction-sections
  -Wno-unused-parameter -Wno-unused-variable -Wno-unused-label -Wno-unused-but-set-variable)
# CMAKE_CXX_FLAGS_RELEASE adds -O3; STORM2_OPT comes later on the command line
# and wins.
