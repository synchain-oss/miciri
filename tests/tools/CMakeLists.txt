# SPDX-License-Identifier: GPL-3.0-or-later
#
# 命令行工具的测试：整条命令行（参数、模式、退出码、JSON）在内存里的合成 wav 上跑，不读写文件。
# 各模式输出的数值对合成真值；wav 解析与电平类报告的测试在 tests/core/level/analyze_cli_test.cpp。

miciri_add_test(NAME tools_analyze_modes SOURCES analyze_modes_test.cpp LINK miciri_analyze_lib)
