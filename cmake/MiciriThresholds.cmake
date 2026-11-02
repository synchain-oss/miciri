# SPDX-License-Identifier: GPL-3.0-or-later
#
# thresholds.json 进 C++ 的唯一通道：配置期读 JSON、按 schema 校验、生成 <构建目录>/generated/miciri/thresholds_generated.h，
# 并提供接口目标 miciri_thresholds（只带那个头文件的搜索路径）。core 代码经 src/core/config/Thresholds.h 取值，不写魔法数。
# JSON、schema 或生成脚本任何一个改了，CMake 自动重新配置、重新生成，用到它的目标自动重编。校验失败直接让配置失败。

include("${CMAKE_CURRENT_LIST_DIR}/../tools/gen-thresholds.cmake")

set(MICIRI_THRESHOLDS_JSON "${CMAKE_SOURCE_DIR}/thresholds.json")
set(MICIRI_THRESHOLDS_SCHEMA "${CMAKE_SOURCE_DIR}/tools/thresholds-schema.json")
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${MICIRI_THRESHOLDS_JSON}" "${MICIRI_THRESHOLDS_SCHEMA}" "${CMAKE_SOURCE_DIR}/tools/gen-thresholds.cmake")

miciri_generate_thresholds("${MICIRI_THRESHOLDS_JSON}" "${MICIRI_THRESHOLDS_SCHEMA}"
  "${CMAKE_BINARY_DIR}/generated/miciri/thresholds_generated.h")

add_library(miciri_thresholds INTERFACE)
target_include_directories(miciri_thresholds INTERFACE "${CMAKE_BINARY_DIR}/generated")
