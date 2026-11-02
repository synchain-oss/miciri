# SPDX-License-Identifier: GPL-3.0-or-later
# Derived from synchain-oss/rinne; see upstream/cmake.json
#
# Miciri 的构建入口。版本只有这一个真源：project(Miciri VERSION …)。配置：
#   cmake -S . -B build -G Ninja -DJUCE_PATH=<JUCE 检出，提交见 .juce-commit>
#   cmake -S . -B build -G Ninja -DMICIRI_CORE_ONLY=ON     只建 core 与测试，不需要 JUCE
# 本机与 CI 都经 scripts/build.ps1 调用。后续的卡只新增文件，不改这里，扩展点是：
#   src/core/<模块>/*.cpp            自动并入 miciri_core（见 src/core/CMakeLists.txt）
#   src/<目录>/CMakeLists.txt        存在就加入（plugin、session 等）；依赖 JUCE 的目录在 MICIRI_CORE_ONLY 下自己 return()
#   tools/<名字>/CMakeLists.txt      存在就加入
#   tests/**/CMakeLists.txt          存在就加入，用 miciri_add_test 登记（见 tests/CMakeLists.txt）
cmake_minimum_required(VERSION 3.22)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# 静态 CRT 必须在 project() 之前设：插件不依赖宿主机器上装了 VC 运行库。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MICIRI_CORE_ONLY "只构建 core 与它的测试，不需要 JUCE（sanitizer 构建用）" OFF)
option(MICIRI_BUILD_TESTS "构建测试" ON)
option(MICIRI_WARNINGS_AS_ERRORS "第一方代码的告警即错误" OFF)
set(MICIRI_SANITIZER "none" CACHE STRING "只作用于 core 与测试：none、asan_ubsan 或 tsan")
set_property(CACHE MICIRI_SANITIZER PROPERTY STRINGS none asan_ubsan tsan)
if(NOT MICIRI_SANITIZER MATCHES "^(none|asan_ubsan|tsan)$")
  message(FATAL_ERROR "MICIRI_SANITIZER 只能是 none、asan_ubsan 或 tsan，现在是 ${MICIRI_SANITIZER}")
endif()

# macOS 只出 arm64（Intel 上无法实测），最低系统 11.0。架构与部署目标参与编译器探测，必须在 project() 之前；
# 命令行或缓存里已有值就不覆盖。这时 APPLE 还没有定义，所以按宿主系统名判断。
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS 目标架构")
  endif()
  if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS 最低系统版本")
  endif()
endif()

project(Miciri VERSION 0.1.0 LANGUAGES CXX)

include(MiciriWarnings)
include(MiciriThresholds)

# 插件身份：src/plugin 的 juce_add_plugin 读这些变量，不在别处再写一份。
# 只出 VST3（Windows、macOS）与 AU（macOS）；不出 Standalone 与 AAX。
set(MICIRI_COMPANY "Synchain")
set(MICIRI_MANUFACTURER_CODE "Snch")
set(MICIRI_PLUGIN_CODE "Mcri")
set(MICIRI_BUNDLE_ID "com.synchain.miciri")
set(MICIRI_VST3_CATEGORY "Fx|Analyzer")
set(MICIRI_PLUGIN_FORMATS VST3)
if(APPLE)
  list(APPEND MICIRI_PLUGIN_FORMATS AU)
endif()
set(MICIRI_PLUGIN_DEFINITIONS JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_WEB_BROWSER=1)
if(WIN32)
  list(APPEND MICIRI_PLUGIN_DEFINITIONS JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
endif()

if(NOT MICIRI_CORE_ONLY)
  include(MiciriJuceGuard)
  add_subdirectory("${_miciri_juce_dir}" "${CMAKE_BINARY_DIR}/juce")
endif()

if(MICIRI_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(src/core)
file(GLOB _miciri_dirs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*/CMakeLists.txt")
foreach(_f IN LISTS _miciri_dirs)
  get_filename_component(_d "${_f}" DIRECTORY)
  get_filename_component(_n "${_d}" NAME)
  if(NOT _n STREQUAL "core")
    add_subdirectory("${_d}")
  endif()
endforeach()
if(MICIRI_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()
file(GLOB _miciri_tools CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/*/CMakeLists.txt")
foreach(_f IN LISTS _miciri_tools)
  get_filename_component(_d "${_f}" DIRECTORY)
  add_subdirectory("${_d}")
endforeach()
