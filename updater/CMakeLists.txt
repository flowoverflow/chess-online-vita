cmake_minimum_required(VERSION 3.16)

# Chess Online Updater (CHESSUPD1): the helper that installs Chess Online's updates (1.0.1; src/main.c).
# Unsafe homebrew: it writes ux0:app/CHESSONL1. Build with scripts/chessonline/build-updater.sh.

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(chessonline_updater C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Chess Online Updater")
set(VITA_TITLEID "CHESSUPD1")
set(VITA_VERSION "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation")

add_executable(updater src/main.c ../src/portable/sfo.c)
target_include_directories(updater PRIVATE ../src/portable)
target_link_libraries(updater
  vita2d freetype bz2 png16 jpeg z m
  SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub SceAppUtil_stub SceCommonDialog_stub
  ScePgf_stub ScePvf_stub SceAppMgr_stub SceRtc_stub SceIofilemgr_stub SceLibKernel_stub SceKernelThreadMgr_stub
  SceProcessmgr_stub
)

vita_create_self(eboot.bin updater UNSAFE)
vita_create_vpk(ChessOnlineUpdater.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE res/sce_sys/icon0.png sce_sys/icon0.png
  FILE res/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE res/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE res/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_SOURCE_DIR}/../res/logo140.png res/logo140.png
)
