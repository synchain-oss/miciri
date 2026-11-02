# SPDX-License-Identifier: GPL-3.0-or-later
#
# 跑一次阈值生成脚本，要求它非零退出，并且报错里有 EXPECT：
#   cmake -DGENERATOR=<脚本> -DINPUT=<json> -DSCHEMA=<schema> -DOUTPUT=<头文件> -DEXPECT=<片段> -P expect-reject.cmake
# ctest 的 PASS_REGULAR_EXPRESSION 不看退出码，只报错不退出也会放行，所以包这一层，两条都查。

cmake_policy(VERSION 3.22)

execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DINPUT=${INPUT}" "-DSCHEMA=${SCHEMA}" "-DOUTPUT=${OUTPUT}" -P "${GENERATOR}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)
if(rc EQUAL 0)
  message(FATAL_ERROR "expected the generator to reject the input, but it exited 0\n${err}")
endif()
if(NOT "${err}" MATCHES "${EXPECT}")
  message(FATAL_ERROR "the generator failed with the wrong message; wanted: ${EXPECT}\n${err}")
endif()
