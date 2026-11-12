cmake_minimum_required(VERSION 3.16)

# Chess Online: PS Vita chess app (guest games on Coolmath's chess server, our own engine). Regular (hard-float) VitaSDK 2026.08.
# Build with scripts/chessonline/build.sh. Personal use only; the VPK stays on the user's console.

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(chessonline C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Chess Online")
set(VITA_TITLEID "CHESSONL1")
set(VITA_VERSION "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers")

add_executable(chessonline
  src/vita/main.c
  src/vita/net.c
  src/vita/ui.c
  src/vita/platform.c
  src/vita/guest.c
  src/vita/boardui.c
  src/vita/localgame.c
  src/vita/textpad.c
  src/vita/tips.c
  src/vita/statsui.c
  src/vita/langui.c
  src/portable/gamelog.c
  src/portable/i18n.c
  src/portable/i18n_table.c
  src/portable/chess.c
  src/portable/json.c
  src/portable/engine.c
  src/portable/cmg.c
  src/portable/lessons.c
)
target_include_directories(chessonline PRIVATE src/portable src/vita)
# see src/vita/textpad.c; set by scripts/chessonline/build.sh after a "segment 1 overlaps"
set(CO_TEXT_PAD 1 CACHE STRING "bytes of text-segment pad")
set_source_files_properties(src/vita/textpad.c PROPERTIES COMPILE_DEFINITIONS "CO_TEXT_PAD=${CO_TEXT_PAD}")

target_link_libraries(chessonline
  vita2d
  freetype bz2 png16 jpeg
  curl ssl crypto zstd z
  m pthread
  SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub SceTouch_stub
  SceAppUtil_stub SceCommonDialog_stub SceNet_stub SceNetCtl_stub SceRtc_stub
  ScePgf_stub ScePvf_stub SceAppMgr_stub SceProcessmgr_stub SceKernelThreadMgr_stub
  SceKernelModulemgr_stub SceIofilemgr_stub SceSysmem_stub SceLibKernel_stub SceHttp_stub SceSsl_stub
)

vita_create_self(eboot.bin chessonline)

# every file under res/: sce_sys/* keep their place, the rest go to res/ in the VPK
file(GLOB_RECURSE RES_FILES RELATIVE "${CMAKE_SOURCE_DIR}/res" "${CMAKE_SOURCE_DIR}/res/*")
set(VPK_FILES "")
foreach(f ${RES_FILES})
  if(f MATCHES "^sce_sys/")
    list(APPEND VPK_FILES FILE "${CMAKE_SOURCE_DIR}/res/${f}" "${f}")
  else()
    list(APPEND VPK_FILES FILE "${CMAKE_SOURCE_DIR}/res/${f}" "res/${f}")
  endif()
endforeach()

vita_create_vpk(chessonline.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  ${VPK_FILES}
)
