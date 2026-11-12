# SPDX-License-Identifier: GPL-3.0-or-later
#
# miciri_session：步骤状态机、采集环、工作线程与快照。不依赖 JUCE，只建 core 的构建里也编，
# 这样 sanitizer 构建（含 TSan）能覆盖音频线程、工作线程与控制线程之间的交接。
# 头文件按 "session/<文件>.h" 引用，include 根是 src/；桥契约的名表按 "plugin/BridgeApi.h" 引用（它不依赖 JUCE）。

find_package(Threads REQUIRED)
file(GLOB _miciri_session_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(miciri_session STATIC ${_miciri_session_sources})
target_include_directories(miciri_session PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(miciri_session PUBLIC miciri_core Threads::Threads)

miciri_target_warnings(miciri_session PRIVATE)
miciri_target_strict_fp(miciri_session)
miciri_target_sanitize(miciri_session)
miciri_warnings_as_errors(miciri_session)
