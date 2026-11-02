# SPDX-License-Identifier: GPL-3.0-or-later
#
# miciri_core：不依赖 JUCE、double 精度的分析库。src/core/<模块>/ 下的 *.cpp 自动并入（CONFIGURE_DEPENDS），
# 新模块只新增目录，不改本文件。头文件按 "<模块>/<文件>.h" 引用，include 根是 src/core。

file(GLOB_RECURSE _miciri_core_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/*.cpp")
add_library(miciri_core STATIC ${_miciri_core_sources})
target_include_directories(miciri_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(miciri_core PUBLIC cxx_std_17)
target_link_libraries(miciri_core PUBLIC miciri_thresholds)

# 第三方的头文件库（FFT）：目录存在才加入，按系统头处理，不吃第一方的告警口径
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/pocketfft")
  target_include_directories(miciri_core SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/pocketfft")
endif()

miciri_target_warnings(miciri_core PRIVATE)
miciri_target_strict_fp(miciri_core)
miciri_target_sanitize(miciri_core)
miciri_warnings_as_errors(miciri_core)
