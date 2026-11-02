# SPDX-License-Identifier: GPL-3.0-or-later
# Derived from synchain-oss/rinne; see upstream/cmake.json
#
# 第一方代码的告警、数值与 sanitizer 口径：
#   miciri_target_warnings(<目标> <PRIVATE|INTERFACE|PUBLIC>)
#       告警等级：MSVC 用 /W4（另加 /utf-8，注释是中文），其余编译器用 -Wall -Wextra -Wpedantic。
#   miciri_warnings_as_errors(<目标>)
#       MICIRI_WARNINGS_AS_ERRORS=ON 时，把目标自己的 .c/.cpp/.mm 标成告警即错误。要在目标所在目录的 CMakeLists.txt 末尾调用，
#       那时源文件列表已定。JUCE 模块的源码不在目标自己的 SOURCES 里，不受影响；增量构建也安全，因为带告警的文件没有目标文件，下次必重编。
#   miciri_target_strict_fp(<目标>)
#       分析结果要可复现：MSVC 用 /fp:precise，其余编译器显式关掉放宽浮点的选项。
#   miciri_target_sanitize(<目标>)
#       MICIRI_SANITIZER 为 asan_ubsan 或 tsan 时，给目标加 PUBLIC 的编译与链接选项（测试链 core 时自动继承）。只支持 GCC 与 Clang。

function(miciri_target_warnings target scope)
  if(MSVC)
    target_compile_options(${target} ${scope} "/W4" /utf-8)
  else()
    target_compile_options(${target} ${scope} -Wall -Wextra -Wpedantic)
  endif()
endfunction()

function(miciri_warnings_as_errors target)
  if(NOT MICIRI_WARNINGS_AS_ERRORS)
    return()
  endif()
  get_target_property(_sources ${target} SOURCES)
  list(FILTER _sources INCLUDE REGEX "\\.(c|cc|cpp|mm)$")
  if(NOT _sources)
    return()
  endif()
  if(MSVC)
    set_property(SOURCE ${_sources} APPEND PROPERTY COMPILE_OPTIONS /WX)
  else()
    set_property(SOURCE ${_sources} APPEND PROPERTY COMPILE_OPTIONS -Werror)
  endif()
endfunction()

function(miciri_target_strict_fp target)
  if(MSVC)
    target_compile_options(${target} PUBLIC /fp:precise)
  else()
    target_compile_options(${target} PUBLIC -fno-fast-math -ffp-contract=off)
  endif()
endfunction()

function(miciri_target_sanitize target)
  if(MICIRI_SANITIZER STREQUAL "none")
    return()
  endif()
  if(MSVC)
    message(FATAL_ERROR "MICIRI_SANITIZER=${MICIRI_SANITIZER} 只支持 GCC 与 Clang")
  endif()
  if(MICIRI_SANITIZER STREQUAL "tsan")
    set(_flags -fsanitize=thread)
  else()
    set(_flags -fsanitize=address,undefined -fno-sanitize-recover=undefined)
  endif()
  target_compile_options(${target} PUBLIC ${_flags} -fno-omit-frame-pointer)
  target_link_options(${target} PUBLIC ${_flags})
endfunction()
