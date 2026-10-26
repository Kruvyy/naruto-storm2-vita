# PS Vita memory probe: free memory, largest and total allocation per memory
# block type, and CPU access speed, for a normal app and for one with the
# extended-memory attribute (ATTRIBUTE2=12).
#
#   cmake -G Ninja -S vita/memprobe -B vita/memprobe/build-vita ^
#     -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake
#   cmake --build vita/memprobe/build-vita
cmake_minimum_required(VERSION 3.20)
project(rexglue_memprobe CXX C)
set(CMAKE_CXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# A function keeps VITA_MKSFOEX_FLAGS (accumulated by the vita_create_vpk
# macro) separate for each variant.
function(memprobe_variant variant titleid label sfo_flags)
  add_executable(memprobe_${variant} memprobe.cpp)
  target_compile_definitions(memprobe_${variant} PRIVATE MEMPROBE_VARIANT="${variant}")
  target_link_libraries(memprobe_${variant} PRIVATE
    SceDisplay_stub SceCtrl_stub SceSysmem_stub SceIofilemgr_stub SceLibKernel_stub
    SceKernelThreadMgr_stub SceProcessmgr_stub)
  vita_create_self(eboot_${variant}.bin memprobe_${variant})
  set(VITA_MKSFOEX_FLAGS "${sfo_flags}")
  vita_create_vpk(rexglue_memprobe_${variant}.vpk ${titleid} eboot_${variant}.bin
    VERSION "01.00"
    NAME "${label}")
endfunction()

memprobe_variant(normal RXGT00004 "ReXGlue Mem Probe" "")
memprobe_variant(extended RXGT00005 "ReXGlue Mem Probe Ext" "-d ATTRIBUTE2=12")
