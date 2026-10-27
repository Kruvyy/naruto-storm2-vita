# G0 of the renderer: runtime shader compiler (vitaShaRK) and vitaGL drawing,
# render-to-texture and read-back. Log: ux0:data/storm2port/gltest.log.
#
#   cmake -G Ninja -S vita/gltest -B vita/gltest/build-vita ^
#     -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake
#   cmake --build vita/gltest/build-vita
cmake_minimum_required(VERSION 3.20)
project(rexglue_gltest CXX C)
set(CMAKE_CXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

add_executable(gltest gltest.cpp)
target_link_libraries(gltest PRIVATE
  vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub
  SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub SceAppMgr_stub SceCommonDialog_stub
  SceIme_stub SceSysmodule_stub SceCtrl_stub SceIofilemgr_stub SceLibKernel_stub
  SceKernelThreadMgr_stub SceProcessmgr_stub SceSysmem_stub m pthread)
vita_create_self(eboot.bin gltest)
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
vita_create_vpk(rexglue_gltest.vpk RXGT00007 eboot.bin
  VERSION "01.00"
  NAME "ReXGlue GL Test")
