# Codegen register-model benchmark for the PS Vita (and the desktop host, to
# validate the harness). Inputs: vita/bench/gen/<variant>/ produced by
# tools/make_variants.py from the bench.* PPC cases.
#
# Vita:  cmake -G Ninja -S vita/bench -B vita/bench/build-vita \
#          -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake
# Host:  cmake -G Ninja -S vita/bench -B vita/bench/build-host -DCMAKE_CXX_COMPILER=clang++
cmake_minimum_required(VERSION 3.20)

if(DEFINED ENV{VITASDK} AND CMAKE_TOOLCHAIN_FILE MATCHES "vita")
  set(REX_VITA_BUILD ON)
endif()

project(rexglue_bench_vita CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(REXGLUE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../rexglue" CACHE PATH "ReXGlue checkout")
set(BENCH_GEN_DIR "${CMAKE_CURRENT_LIST_DIR}/gen" CACHE PATH "make_variants.py output")
set(BENCH_VARIANTS v0 v1 v2 v3 v4)

add_executable(bench_vita src/bench_main.cpp)
target_include_directories(bench_vita BEFORE PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../ppc_tests/shim
  ${CMAKE_CURRENT_LIST_DIR}/../ppc_tests/src
  ${BENCH_GEN_DIR}
  ${BENCH_GEN_DIR}/v0
  ${REXGLUE_DIR}/include
  ${REXGLUE_DIR}/thirdparty/simde)

# One object library per variant so each sees its own generated headers.
foreach(v ${BENCH_VARIANTS})
  add_library(bench_${v} OBJECT
    ${BENCH_GEN_DIR}/${v}/ppc_test_functions.cpp
    ${BENCH_GEN_DIR}/${v}/cases.cpp)
  target_include_directories(bench_${v} BEFORE PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../ppc_tests/shim
    ${CMAKE_CURRENT_LIST_DIR}/../ppc_tests/src
    ${BENCH_GEN_DIR}/${v}
    ${REXGLUE_DIR}/include
    ${REXGLUE_DIR}/thirdparty/simde)
  # 64 KiB pages: the harness builds its own page table (src/bench_main.cpp).
  target_compile_definitions(bench_${v} PRIVATE REX_GUEST_SOFT_MMU REX_GUEST_SPLIT_U64
                                                REX_GUEST_PAGE_SHIFT=16)
  target_compile_options(bench_${v} PRIVATE -fwrapv -Wno-unused-parameter -Wno-unused-variable)
  if(NOT REX_VITA_BUILD)
    target_compile_options(bench_${v} PRIVATE -msse4.1 -mssse3)
  endif()
  target_sources(bench_vita PRIVATE $<TARGET_OBJECTS:bench_${v}>)
endforeach()

target_compile_definitions(bench_vita PRIVATE REX_GUEST_SOFT_MMU REX_GUEST_SPLIT_U64
                                              REX_GUEST_PAGE_SHIFT=16)
target_compile_options(bench_vita PRIVATE -fwrapv -Wno-unused-parameter -Wno-unused-variable)

if(REX_VITA_BUILD)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  target_sources(bench_vita PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common/vita_math_compat.c)
  target_link_libraries(bench_vita PRIVATE
    SceDisplay_stub SceCtrl_stub SceSysmem_stub SceIofilemgr_stub
    SceLibKernel_stub SceKernelThreadMgr_stub SceProcessmgr_stub ScePower_stub pthread)
  vita_create_self(eboot.bin bench_vita)
  vita_create_vpk(rexglue_bench.vpk RXGT00002 eboot.bin
    VERSION "01.00"
    NAME "ReXGlue Bench")
else()
  target_compile_options(bench_vita PRIVATE -msse4.1 -mssse3)
endif()
