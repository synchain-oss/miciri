# SPDX-License-Identifier: GPL-3.0-or-later
# Derived from synchain-oss/rinne; see upstream/cmake.json
#
# 第三方依赖，只给测试用，不进插件：根 CMakeLists.txt 在 MICIRI_BUILD_TESTS 为 ON 时 include 本文件。
# Catch2 钉在 tag v3.5.4 所指的 40 位提交上：tag 可以被移动，提交不会。不用 GIT_SHALLOW，因为浅克隆只认分支和 tag 名。
# 升版本时提交与行尾的版本注释一起改。以后新增依赖在这里声明，用到的目标自己 link，不改别处的共享文件。

include(FetchContent)

set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG b5373dadca40b7edc8570cf9470b9b1cb1934d40) # v3.5.4
FetchContent_MakeAvailable(Catch2)

# 第三方头文件当系统头文件：第一方的告警即错误不该被 Catch2 的宏牵连
get_target_property(_miciri_catch2_inc Catch2 INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(Catch2 PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_miciri_catch2_inc}")
