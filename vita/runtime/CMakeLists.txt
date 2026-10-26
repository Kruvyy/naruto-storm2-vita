# ReXGlue runtime for the PS Vita (B3 of docs/PLANO_RUNTIME_VITA.md).
#
# Builds, with the VitaSDK, the parts of the ReXGlue runtime that do not
# depend on a window, a GPU or audio output: core (with the *_vita.cpp
# platform layer), system, kernel and filesystem. They use the software MMU
# (REXGLUE_SOFT_MMU) and the code generator's 32-bit memory model.
#
#   cmake -G Ninja -S vita/runtime -B vita/runtime/build-vita ^
#     -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake
#   cmake --build vita/runtime/build-vita
cmake_minimum_required(VERSION 3.20)
project(rexglue_vita_runtime C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(REXGLUE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../rexglue" CACHE PATH "ReXGlue checkout")
set(TP "${REXGLUE_DIR}/thirdparty")
set(SRC "${REXGLUE_DIR}/src")

#=============================================================================
# Third-party libraries (same sources and options as rexglue/thirdparty)
#=============================================================================
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/fmt fmt EXCLUDE_FROM_ALL)
target_compile_options(fmt PRIVATE -w)
# flockfile/funlockfile, declared but missing in the SDK's newlib; fmt uses
# them, so they are linked after it.
add_library(vita_compat STATIC ${CMAKE_CURRENT_LIST_DIR}/compat/stdio_lock.c)
target_link_libraries(fmt PUBLIC vita_compat)

set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
# newlib has no fwrite_unlocked and no tm_gmtoff (the log shows no UTC offset).
set(SPDLOG_FWRITE_UNLOCKED OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/spdlog spdlog EXCLUDE_FROM_ALL)
target_compile_options(spdlog PRIVATE -w)
target_compile_definitions(spdlog PUBLIC SPDLOG_NO_TZ_OFFSET PRIVATE _GNU_SOURCE)

set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/xxHash/cmake_unofficial xxhash EXCLUDE_FROM_ALL)
target_compile_options(xxhash PRIVATE -w)

add_subdirectory(${TP}/utfcpp utfcpp EXCLUDE_FROM_ALL)

add_library(disruptorplus INTERFACE)
target_include_directories(disruptorplus INTERFACE ${TP}/disruptorplus/include)
add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus SYSTEM INTERFACE ${TP}/tomlplusplus/include)
add_library(simde INTERFACE)
target_include_directories(simde INTERFACE ${TP}/simde ${TP}/simde/simde)

add_library(aes128 STATIC ${TP}/aes_128/aes.c)
target_include_directories(aes128 PUBLIC ${TP})
target_compile_options(aes128 PRIVATE -w)

set(MSPACK_DIR "${TP}/libmspack/cabextract/mspack")
add_library(mspack STATIC ${MSPACK_DIR}/lzxd.c)
target_include_directories(mspack PUBLIC ${MSPACK_DIR})
target_compile_options(mspack PRIVATE -w)

add_library(o1heap STATIC ${TP}/o1heap/o1heap/o1heap.c)
target_include_directories(o1heap PUBLIC ${TP}/o1heap/o1heap)
target_compile_definitions(o1heap PRIVATE
  "O1HEAP_CONFIG_HEADER=\"${REXGLUE_DIR}/include/rex/o1heap_config.h\"")
target_compile_options(o1heap PRIVATE -w)

#=============================================================================
# Settings shared by the runtime libraries
#=============================================================================
# rex/version.h (the desktop build generates it from cmake/rex_version.cmake).
set(REX_VITA_GEN_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/include")
file(WRITE "${REX_VITA_GEN_INCLUDE}/rex/version.h" [=[
#pragma once
// Written by vita/runtime/CMakeLists.txt
#define REXGLUE_VERSION_MAJOR 0
#define REXGLUE_VERSION_MINOR 10
#define REXGLUE_VERSION_PATCH 0
#define REXGLUE_VERSION_TWEAK 0
#define REXGLUE_VERSION_STRING "0.10.0.0-vita"
#define REXGLUE_VERSION_NUMERIC "0.10.0.0"
#define REXGLUE_VERSION_FLOOR "0.10.0"
#define REXGLUE_VERSION_CHANNEL "dev"
#define REXGLUE_BUILD_PLATFORM "vita-arm32"
#define REXGLUE_BUILD_TIMESTAMP "vita"
#define REXGLUE_BUILD_TITLE "[rexglue-v" REXGLUE_VERSION_STRING "-" REXGLUE_BUILD_CONFIG "]"
#define REXGLUE_BUILD_STAMP \
  "build: rexglue-v" REXGLUE_VERSION_STRING "-" REXGLUE_BUILD_PLATFORM "-" REXGLUE_BUILD_CONFIG
]=])

add_library(rex_vita_settings INTERFACE)
# Function/data sections let executables drop runtime code they never reach.
target_compile_options(rex_vita_settings INTERFACE -ffunction-sections -fdata-sections)
target_include_directories(rex_vita_settings INTERFACE
  ${REXGLUE_DIR}/include ${REX_VITA_GEN_INCLUDE})
target_compile_definitions(rex_vita_settings INTERFACE
  REXGLUE_SOFT_MMU=1 REX_GUEST_SOFT_MMU REX_GUEST_SPLIT_U64
  REXGLUE_BUILD_CONFIG="Release")
# Same global options as the desktop build (rexglue/CMakeLists.txt), with the
# GCC spelling of clang's -ffp-model=strict (no FMA contraction).
target_compile_options(rex_vita_settings INTERFACE
  -fno-strict-aliasing -ffp-contract=off
  $<$<COMPILE_LANGUAGE:CXX>:-fno-char8_t -Wno-psabi> -Wno-unused-parameter)
target_link_libraries(rex_vita_settings INTERFACE fmt::fmt spdlog::spdlog tomlplusplus simde)

#=============================================================================
# rexcore: core utilities + PS Vita platform layer
#=============================================================================
set(REXCORE_VITA_SOURCES
  arena.cpp bit_map.cpp bit_stream.cpp byte_stream.cpp clock.cpp cvar.cpp
  exception_handler.cpp filesystem.cpp filesystem_wildcard.cpp hash.cpp logging.cpp
  memory.cpp mutex.cpp ring_buffer.cpp string_buffer.cpp threading.cpp timer_queue.cpp
  utf8.cpp vec128.cpp
  # PS Vita platform layer
  atomic_posix.cpp clock_vita.cpp dbg_vita.cpp dynlib_vita.cpp exception_handler_vita.cpp
  filesystem_vita.cpp fiber_vita.cpp mapped_memory_vita.cpp math_gcc.cpp memory_vita.cpp
  platform/console_vita.cpp platform/env_posix.cpp seh_vita.cpp socket_vita.cpp
  system_posix.cpp threading_vita.cpp)
list(TRANSFORM REXCORE_VITA_SOURCES PREPEND "${SRC}/core/")
list(APPEND REXCORE_VITA_SOURCES
  ${SRC}/graphics/xenos.cpp
  ${SRC}/graphics/pipeline/texture/info.cpp
  ${SRC}/graphics/pipeline/texture/info_formats.cpp
  ${SRC}/graphics/pipeline/texture/extent.cpp
  ${SRC}/graphics/pipeline/texture/util.cpp
  ${SRC}/graphics/pipeline/texture/conversion.cpp)

add_library(rexcore_vita STATIC ${REXCORE_VITA_SOURCES})
target_include_directories(rexcore_vita PRIVATE ${REXGLUE_DIR} ${TP}/cli11/include)
target_link_libraries(rexcore_vita PUBLIC rex_vita_settings
  PRIVATE disruptorplus utf8cpp xxHash::xxhash)

#=============================================================================
# rexruntime: system + kernel + filesystem (lists as in src/*/CMakeLists.txt)
#=============================================================================
set(REXSYSTEM_VITA_SOURCES
  runtime.cpp embedded_metadata.cpp achievement_manager.cpp achievements.cpp kernel_state.cpp
  xmemory.cpp kernel_module.cpp user_module.cpp xthread.cpp xevent.cpp xfile.cpp xmutant.cpp
  xsemaphore.cpp xtimer.cpp xiocompletion.cpp xnotifylistener.cpp xenumerator.cpp xsocket.cpp
  xsymboliclink.cpp xobject.cpp xmodule.cpp guest_path.cpp
  util/object_table.cpp util/native_list.cpp util/xdbf_utils.cpp util/gameinfo_utils.cpp
  xam/app_manager.cpp xam/content_manager.cpp xam/user_profile.cpp
  ppc_types.cpp elf_module.cpp entry_table.cpp export_resolver.cpp lzx.cpp mmio_handler.cpp
  map_parser.cpp module.cpp function_dispatcher.cpp guest_frame_stats.cpp gpu_plugin_loader.cpp
  thread.cpp thread_state.cpp xex_module.cpp)
list(TRANSFORM REXSYSTEM_VITA_SOURCES PREPEND "${SRC}/system/")

set(REXKERNEL_VITA_SOURCES
  kernel_init.cpp
  xam/xam_module.cpp xam/xam_avatar.cpp xam/xam_content.cpp xam/xam_content_aggregate.cpp
  xam/xam_content_device.cpp xam/xam_debug.cpp xam/xam_enum.cpp xam/xam_info.cpp
  xam/xam_input.cpp xam/xam_locale.cpp xam/xam_msg.cpp xam/xam_net.cpp xam/xam_notify.cpp
  xam/xam_nui.cpp xam/xam_party.cpp xam/xam_task.cpp xam/xam_ui.cpp xam/xam_user.cpp
  xam/xam_video.cpp xam/xam_voice.cpp xam/xam_misc.cpp
  xam/apps/xam_app.cpp xam/apps/xgi_app.cpp xam/apps/xlivebase_app.cpp xam/apps/xmp_app.cpp
  xbdm/xbdm_module.cpp xbdm/xbdm_misc.cpp
  xboxkrnl/xboxkrnl_crypt.cpp xboxkrnl/xboxkrnl_debug.cpp xboxkrnl/xboxkrnl_error.cpp
  xboxkrnl/xboxkrnl_hal.cpp xboxkrnl/xboxkrnl_hid.cpp xboxkrnl/xboxkrnl_io.cpp
  xboxkrnl/xboxkrnl_io_info.cpp xboxkrnl/xboxkrnl_memory.cpp xboxkrnl/xboxkrnl_misc.cpp
  xboxkrnl/xboxkrnl_module.cpp xboxkrnl/xboxkrnl_modules.cpp xboxkrnl/xboxkrnl_ob.cpp
  xboxkrnl/xboxkrnl_rtl.cpp xboxkrnl/xboxkrnl_strings.cpp xboxkrnl/xboxkrnl_threading.cpp
  xboxkrnl/xboxkrnl_video.cpp xboxkrnl/xboxkrnl_xconfig.cpp xboxkrnl/xboxkrnl_audio.cpp
  xboxkrnl/xboxkrnl_audio_xma.cpp
  crt/heap.cpp crt/file.cpp crt/memory.cpp crt/string.cpp crt/threading.cpp)
list(TRANSFORM REXKERNEL_VITA_SOURCES PREPEND "${SRC}/kernel/")

set(REXFILESYSTEM_VITA_SOURCES
  device.cpp entry.cpp virtual_file_system.cpp
  devices/disc_image_device.cpp devices/disc_image_entry.cpp devices/disc_image_file.cpp
  devices/host_path_device.cpp devices/host_path_entry.cpp devices/host_path_file.cpp
  devices/null_device.cpp devices/null_entry.cpp devices/null_file.cpp
  devices/stfs_container_device.cpp devices/stfs_container_entry.cpp
  devices/stfs_container_file.cpp)
list(TRANSFORM REXFILESYSTEM_VITA_SOURCES PREPEND "${SRC}/filesystem/")

add_library(rexruntime_vita STATIC
  ${REXSYSTEM_VITA_SOURCES} ${REXKERNEL_VITA_SOURCES} ${REXFILESYSTEM_VITA_SOURCES})
# compat/: <endian.h> for thirdparty/crypto.
target_include_directories(rexruntime_vita PRIVATE
  ${TP} ${TP}/imgui ${SRC}/filesystem/devices ${CMAKE_CURRENT_LIST_DIR}/compat)
target_link_libraries(rexruntime_vita PUBLIC rexcore_vita
  PRIVATE aes128 mspack o1heap xxHash::xxhash utf8cpp)

#=============================================================================
# Runtime tests VPK (ux0:data/storm2port/runtime_tests.log)
#=============================================================================
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
add_executable(runtime_tests_vita
  tests/runtime_tests.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../common/vita_math_compat.c)
# Unused sections are dropped, so objects of the static runtime that the tests
# pull in do not drag in subsystems that are not built here (UI, audio, input).
target_compile_options(runtime_tests_vita PRIVATE -ffunction-sections -fdata-sections)
target_link_options(runtime_tests_vita PRIVATE -Wl,--gc-sections)
# pthread as a whole archive: libstdc++ decides that threads are available
# from weak references to pthread symbols, and std::thread throws
# "Enable multithreading to use std::thread" when they are missing.
target_link_libraries(runtime_tests_vita PRIVATE rexruntime_vita rexcore_vita
  SceDisplay_stub SceCtrl_stub SceSysmem_stub SceIofilemgr_stub SceLibKernel_stub
  SceKernelThreadMgr_stub SceProcessmgr_stub SceRtc_stub SceFiber_stub SceSysmodule_stub
  -Wl,--whole-archive pthread -Wl,--no-whole-archive)
vita_create_self(eboot.bin runtime_tests_vita)
vita_create_vpk(rexglue_runtime_tests.vpk RXGT00003 eboot.bin
  VERSION "01.00"
  NAME "ReXGlue Runtime Tests")
