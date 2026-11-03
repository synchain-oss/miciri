# SPDX-License-Identifier: GPL-3.0-or-later
#
# 命令行分析工具：读 wav，输出各项 measures 与 verdict 的 JSON。不依赖 JUCE。
# 库与可执行文件分开，测试直接链库、用内存里的字节喂解析器。

add_library(miciri_analyze_lib STATIC Wav.cpp Report.cpp)
target_include_directories(miciri_analyze_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(miciri_analyze_lib PUBLIC miciri_core)
miciri_target_warnings(miciri_analyze_lib PRIVATE)
miciri_warnings_as_errors(miciri_analyze_lib)

add_executable(miciri-analyze main.cpp)
target_link_libraries(miciri-analyze PRIVATE miciri_analyze_lib)
miciri_target_warnings(miciri-analyze PRIVATE)
miciri_warnings_as_errors(miciri-analyze)
