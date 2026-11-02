# SPDX-License-Identifier: GPL-3.0-or-later
#
# 测试入口。tests/ 下任何带 CMakeLists.txt 的子目录自动加入（每个目录自己登记，不要再 add_subdirectory 它的子目录），
# 登记用 miciri_add_test：
#   miciri_add_test(NAME <名> SOURCES <文件...> [LINK <库...>] [LABELS <标签...>] [DEFINITIONS <宏...>])
# 每个测试是一个可执行文件加一条 ctest；链 miciri_core，include 根是 src/ 与 tests/；ctest 的工作目录是仓根，
# 读夹具和契约文件用相对仓根的路径。标签 golden 给 DSP golden，alloc 给分配计数器（scripts/build.ps1 按标签跑）。

function(miciri_add_test)
  cmake_parse_arguments(T "" "NAME" "SOURCES;LINK;LABELS;DEFINITIONS" ${ARGN})
  add_executable(${T_NAME} ${T_SOURCES})
  target_link_libraries(${T_NAME} PRIVATE miciri_core ${T_LINK})
  target_include_directories(${T_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/tests")
  if(T_DEFINITIONS)
    target_compile_definitions(${T_NAME} PRIVATE ${T_DEFINITIONS})
  endif()
  miciri_target_warnings(${T_NAME} PRIVATE)
  miciri_warnings_as_errors(${T_NAME})
  add_test(NAME ${T_NAME} COMMAND ${T_NAME} WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  if(T_LABELS)
    set_tests_properties(${T_NAME} PROPERTIES LABELS "${T_LABELS}")
  endif()
endfunction()

file(GLOB_RECURSE _miciri_test_lists CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/CMakeLists.txt")
foreach(_f IN LISTS _miciri_test_lists)
  get_filename_component(_d "${_f}" DIRECTORY)
  add_subdirectory("${_d}")
endforeach()
