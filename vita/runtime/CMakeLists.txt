# ReXGlue runtime for the PS Vita (B3/B4 of docs/PLANO_RUNTIME_VITA.md).
#
# Builds, with the VitaSDK, the ReXGlue runtime without window or renderer:
# core (with the *_vita.cpp platform layer), system, kernel, filesystem, the
# null GPU, input (sceCtrl) and audio (real XMA decoding, silent output). They
# use the software MMU (REXGLUE_SOFT_MMU) and the code generator's 32-bit
# memory model. Also used as a subproject by vita/storm2.
#
#   cmake -G Ninja -S vita/runtime -B vita/runtime/build-vita ^
#     -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake
#   cmake --build vita/runtime/build-vita
cmake_minimum_required(VERSION 3.20)
project(rexglue_vita_runtime C CXX)
option(REX_VITA_RUNTIME_TESTS "Build the runtime tests VPK" ON)

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
add_library(vita_compat STATIC ${CMAKE_CURRENT_LIST_DIR}/compat/stdio_lock.c
  ${CMAKE_CURRENT_LIST_DIR}/compat/fast_tls.c ${CMAKE_CURRENT_LIST_DIR}/compat/heap_trace.c
  ${CMAKE_CURRENT_LIST_DIR}/compat/kernel_objects.c)
target_include_directories(vita_compat PUBLIC ${CMAKE_CURRENT_LIST_DIR}/compat)
# Unwind tables for the heap trace's call paths (compat/heap_trace.c).
target_compile_options(vita_compat PRIVATE -funwind-tables)
# newlib and emutls (thread_local) look up TLS through sceKernelGetTLSAddr;
# compat/fast_tls.c answers from TPIDRURO once a start-up probe has checked it.
# The other wrappers there only count calls (the app's stats).
target_link_options(vita_compat INTERFACE -Wl,--wrap=sceKernelGetTLSAddr
  -Wl,--undefined=__wrap_sceKernelGetTLSAddr
  -Wl,--wrap=sceKernelGetProcessTimeWide -Wl,--wrap=sceKernelGetProcessTimeLow
  -Wl,--wrap=sceKernelGetThreadId -Wl,--wrap=sceKernelLockLwMutex
  -Wl,--wrap=sceKernelTryLockLwMutex -Wl,--wrap=sceKernelUnlockLwMutex
  -Wl,--wrap=sceKernelWaitSema -Wl,--wrap=sceKernelSignalSema
  -Wl,--wrap=sceKernelPollEventFlag -Wl,--wrap=sceKernelDelayThread
  -Wl,--wrap=sceKernelLibcGettimeofday -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=malloc
  -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=memalign)
# compat/kernel_objects.c: live kernel objects by kind (the console limits how
# many a process holds; Vita3K does not).
target_link_options(vita_compat INTERFACE -Wl,--undefined=__wrap_sceKernelCreateSema
  -Wl,--wrap=sceKernelCreateSema -Wl,--wrap=sceKernelDeleteSema
  -Wl,--wrap=sceKernelCreateMutex -Wl,--wrap=sceKernelDeleteMutex
  -Wl,--wrap=sceKernelCreateEventFlag -Wl,--wrap=sceKernelDeleteEventFlag
  -Wl,--wrap=sceKernelCreateLwMutex -Wl,--wrap=sceKernelDeleteLwMutex
  -Wl,--wrap=sceKernelCreateLwCond -Wl,--wrap=sceKernelDeleteLwCond
  -Wl,--wrap=sceKernelCreateCond -Wl,--wrap=sceKernelDeleteCond
  -Wl,--wrap=sceKernelCreateThread -Wl,--wrap=sceKernelDeleteThread
  -Wl,--wrap=sceKernelExitDeleteThread -Wl,--wrap=sceIoOpen -Wl,--wrap=sceIoClose
  -Wl,--wrap=sceIoDopen -Wl,--wrap=sceIoDclose
  -Wl,--wrap=sceKernelAllocMemBlock -Wl,--wrap=sceKernelFreeMemBlock
  -Wl,--wrap=pthread_mutex_init -Wl,--wrap=pthread_mutex_destroy)
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
# FFmpeg (libavutil + libavcodec, the WMA Pro decoder used for XMA): same
# sources as rexglue/thirdparty/CMakeLists.txt without the x86/aarch64 files,
# with a Vita config.h generated from the Linux aarch64 one (plain C).
#=============================================================================
set(FFMPEG_DIR "${TP}/FFmpeg")
set(FFMPEG_VITA_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-config")
find_program(PYTHON_EXE NAMES python3 python REQUIRED)
execute_process(
  COMMAND ${PYTHON_EXE} -I ${CMAKE_CURRENT_LIST_DIR}/tools/make_ffmpeg_config.py
          ${FFMPEG_DIR} ${FFMPEG_VITA_CONFIG_DIR}
  RESULT_VARIABLE FFMPEG_CONFIG_RESULT)
if(NOT FFMPEG_CONFIG_RESULT EQUAL 0)
  message(FATAL_ERROR "make_ffmpeg_config.py failed")
endif()
set(FFMPEG_OVERLAY_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-overlay")
foreach(f codec_list.c parser_list.c bsf_list.c)
  configure_file("${TP}/ffmpeg-overlay/${f}" "${FFMPEG_OVERLAY_BUILD_DIR}/libavcodec/${f}" COPYONLY)
endforeach()

set(LIBAVUTIL_VITA_SOURCES
  adler32.c aes.c aes_ctr.c audio_fifo.c avstring.c avsscanf.c base64.c blowfish.c bprint.c
  buffer.c cast5.c camellia.c channel_layout.c color_utils.c cpu.c crc.c des.c dict.c display.c
  dovi_meta.c downmix_info.c encryption_info.c error.c eval.c fifo.c file.c file_open.c
  float_dsp.c fixed_dsp.c frame.c hash.c hdr_dynamic_metadata.c hmac.c hwcontext.c imgutils.c
  integer.c intmath.c lfg.c lls.c log.c log2_tab.c mathematics.c mastering_display_metadata.c
  md5.c mem.c murmur3.c opt.c parseutils.c pixdesc.c pixelutils.c random_seed.c rational.c
  reverse.c rc4.c ripemd.c samplefmt.c sha.c sha512.c slicethread.c spherical.c stereo3d.c
  threadmessage.c time.c timecode.c tree.c twofish.c utils.c xga_font_data.c xtea.c tea.c tx.c
  tx_float.c tx_double.c tx_int32.c video_enc_params.c film_grain_params.c)
list(TRANSFORM LIBAVUTIL_VITA_SOURCES PREPEND "${FFMPEG_DIR}/libavutil/")
set(LIBAVCODEC_VITA_SOURCES
  ac3_parser.c adts_parser.c allcodecs.c avcodec.c avdct.c avpacket.c avpicture.c bitstream.c
  bitstream_filter.c bitstream_filters.c bsf.c codec_desc.c codec_par.c d3d11va.c decode.c
  dirac.c dv_profile.c encode.c imgconvert.c jni.c mathtables.c mediacodec.c mpeg12framerate.c
  options.c parser.c parsers.c profiles.c qsv_api.c raw.c utils.c vorbis_parser.c xiph.c
  faandct.c faanidct.c fdctdsp.c jfdctfst.c jfdctint.c idctdsp.c simple_idct.c jrevdct.c
  mdct_float.c mdct_fixed_32.c sinewin.c wma_freqs.c wmadec.c wma.c wma_common.c
  mpegaudio.c mpegaudiodata.c mpegaudiodecheader.c mpegaudiodec_common.c mpegaudiodec_float.c
  mpegaudiodec_fixed.c mpegaudiodsp.c mpegaudiodsp_data.c mpegaudiodsp_float.c
  mpegaudiodsp_fixed.c mpegaudio_parser.c dct.c dct32_float.c dct32_fixed.c rdft.c aactab.c
  null_bsf.c pthread.c pthread_slice.c pthread_frame.c avfft.c fft_float.c fft_fixed_32.c
  fft_init_table.c file_open.c)
list(TRANSFORM LIBAVCODEC_VITA_SOURCES PREPEND "${FFMPEG_DIR}/libavcodec/")
# wmaprodec.c with an in-place reset of the XMA frame decoder (rex_xmaframes_reset).
list(APPEND LIBAVCODEC_VITA_SOURCES "${TP}/ffmpeg-overlay/wmaprodec_rex.c")

add_library(libavutil STATIC ${LIBAVUTIL_VITA_SOURCES})
add_library(libavcodec STATIC ${LIBAVCODEC_VITA_SOURCES})
foreach(lib libavutil libavcodec)
  # The Vita config.h must win over FFmpeg's own (which has no Vita case).
  target_include_directories(${lib} BEFORE PRIVATE ${FFMPEG_VITA_CONFIG_DIR})
  target_include_directories(${lib} PUBLIC ${FFMPEG_DIR}
    PRIVATE ${FFMPEG_DIR}/compat/atomics/gcc)
  target_compile_definitions(${lib} PRIVATE HAVE_AV_CONFIG_H _USE_MATH_DEFINES)
  # GCC 14+ turns these old-C diagnostics into errors; this FFmpeg passes enum
  # pointers as int* (same size here: enums are int-sized in this ABI).
  target_compile_options(${lib} PRIVATE -w
    -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
    -Wno-error=implicit-function-declaration)
endforeach()
target_include_directories(libavcodec BEFORE PRIVATE ${FFMPEG_OVERLAY_BUILD_DIR})
target_link_libraries(libavcodec PUBLIC libavutil)
# av_malloc's caller in the heap trace's call paths (compat/heap_trace.c).
target_compile_options(libavutil PRIVATE -funwind-tables)

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
# Diagnostics: count the uses of the global critical region by call site
# (compat/fast_tls.c, printed in the app's stats).
option(REX_VITA_COUNT_GLOBAL_LOCK "Count global lock uses by call site" OFF)
if(REX_VITA_COUNT_GLOBAL_LOCK)
  target_compile_definitions(rexcore_vita PRIVATE REX_VITA_COUNT_GLOBAL_LOCK)
endif()
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
  gpu_write_signal.cpp thread.cpp thread_state.cpp xex_module.cpp)
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

# From src/ui only what the kernel and the GPU reference: the video mode cvars
# and WindowedAppContext (always null here: there is no window or ImGui).
set(REXUI_VITA_SOURCES ${SRC}/ui/display_cvars.cpp ${SRC}/ui/windowed_app_context.cpp)

add_library(rexruntime_vita STATIC
  ${REXSYSTEM_VITA_SOURCES} ${REXKERNEL_VITA_SOURCES} ${REXFILESYSTEM_VITA_SOURCES}
  ${REXUI_VITA_SOURCES})
# compat/: <endian.h> for thirdparty/crypto.
target_include_directories(rexruntime_vita PRIVATE
  ${TP} ${TP}/imgui ${SRC}/filesystem/devices ${CMAKE_CURRENT_LIST_DIR}/compat)
target_link_libraries(rexruntime_vita PUBLIC rexcore_vita
  PRIVATE aes128 mspack o1heap xxHash::xxhash utf8cpp)

#=============================================================================
# rexgpu (null backend only): runs the PM4 command stream, renders nothing.
# Same generic sources as src/graphics/CMakeLists.txt, without the plugin entry
# point and the D3D12/Vulkan backends; the app creates NullGraphicsSystem.
#=============================================================================
set(REXGPU_NULL_VITA_SOURCES
  flags.cpp register_file.cpp registers.cpp pipeline/shader/shader.cpp format/ucode.cpp
  sampler_info.cpp graphics_system.cpp command_processor.cpp util/draw_extent_estimator.cpp
  util/draw.cpp packet_disassembler.cpp primitive_processor.cpp
  pipeline/render_target/cache.cpp shared_memory.cpp pipeline/texture/cache.cpp
  pipeline/shader/interpreter.cpp pipeline/shader/translator.cpp
  pipeline/shader/translator_disasm.cpp null/null_gpu.cpp)
list(TRANSFORM REXGPU_NULL_VITA_SOURCES PREPEND "${SRC}/graphics/")
# util/draw.cpp uses the fixed-point helpers of ui/graphics_util.cpp.
list(APPEND REXGPU_NULL_VITA_SOURCES ${SRC}/ui/graphics_util.cpp)
add_library(rexgpu_null_vita STATIC ${REXGPU_NULL_VITA_SOURCES})
target_include_directories(rexgpu_null_vita PRIVATE ${TP}/renderdoc ${REXGLUE_DIR})
target_compile_options(rexgpu_null_vita PRIVATE -Wno-unused-variable)
target_link_libraries(rexgpu_null_vita PUBLIC rexruntime_vita PRIVATE xxHash::xxhash)

#=============================================================================
# rexgpu (GLES backend through vitaGL): the renderer of docs/PLANO_RENDER_VITA.md.
# Needs ur0:data/external/libshacccg.suprx on the console (runtime shader
# compiler); the app falls back to the null GPU without it.
#=============================================================================
add_library(rexgpu_gles_vita STATIC
  ${SRC}/graphics/pipeline/shader/glsl_translator.cpp
  ${SRC}/graphics/gles/gl_api.cpp ${SRC}/graphics/gles/gles_gpu.cpp
  ${SRC}/graphics/gles/gles_texture.cpp)
target_include_directories(rexgpu_gles_vita PRIVATE ${TP}/renderdoc ${REXGLUE_DIR})
target_compile_options(rexgpu_gles_vita PRIVATE -Wno-unused-variable)
target_link_libraries(rexgpu_gles_vita PUBLIC rexgpu_null_vita PRIVATE xxHash::xxhash)
set(REX_VITA_GLES_LIBS
  vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub SceKernelDmacMgr_stub
  SceGxm_stub SceAppMgr_stub SceCommonDialog_stub SceIme_stub)
if(NOT PROJECT_IS_TOP_LEVEL)
  set(REX_VITA_GLES_LIBS ${REX_VITA_GLES_LIBS} PARENT_SCOPE)
endif()

#=============================================================================
# rexinput: input system (incl. record/replay) with the sceCtrl driver
#=============================================================================
add_library(rexinput_vita STATIC
  ${SRC}/input/input_system.cpp ${SRC}/input/device_assignment.cpp ${SRC}/input/state_merge.cpp
  ${SRC}/input/nop/nop_input_driver.cpp ${SRC}/input/vita/vita_input_driver.cpp)
target_link_libraries(rexinput_vita PUBLIC rexruntime_vita)

#=============================================================================
# rexaudio: audio system with real XMA decoding and a silent real-time driver
#=============================================================================
add_library(rexaudio_vita STATIC
  ${SRC}/audio/audio_driver.cpp ${SRC}/audio/audio_system.cpp ${SRC}/audio/downmix.cpp
  ${SRC}/audio/xma_context.cpp ${SRC}/audio/xma_decoder.cpp ${SRC}/audio/xma_register_file.cpp
  ${SRC}/audio/vita/vita_audio_system.cpp)
target_link_libraries(rexaudio_vita PUBLIC rexruntime_vita PRIVATE libavcodec libavutil)

#=============================================================================
# Runtime tests VPK (ux0:data/storm2port/runtime_tests.log)
#=============================================================================
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
# Libraries a statically linked Vita app on this runtime needs (std::thread
# needs the whole pthread archive, see below).
set(REX_VITA_SYSTEM_LIBS
  SceDisplay_stub SceCtrl_stub SceSysmem_stub SceIofilemgr_stub SceLibKernel_stub
  SceKernelThreadMgr_stub SceProcessmgr_stub SceRtc_stub SceFiber_stub SceSysmodule_stub
  -Wl,--whole-archive pthread -Wl,--no-whole-archive)
if(NOT PROJECT_IS_TOP_LEVEL)
  set(REX_VITA_SYSTEM_LIBS ${REX_VITA_SYSTEM_LIBS} PARENT_SCOPE)
endif()
if(REX_VITA_RUNTIME_TESTS)
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
endif()
