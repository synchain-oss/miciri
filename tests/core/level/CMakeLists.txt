# SPDX-License-Identifier: GPL-3.0-or-later
#
# 电平类分析的测试：加权滤波器对标准表、各项分析的解析真值与块长不变性、golden。
# 重新生成 golden：cmake -DMICIRI_UPDATE_GOLDEN=ON 后运行 golden_dsp，写完记得关掉并附新旧差异说明。

option(MICIRI_UPDATE_GOLDEN "让 golden_dsp 重写 tests/golden/dsp/measures.txt，而不是比对" OFF)

miciri_add_test(NAME level_weighting SOURCES weighting_test.cpp)
miciri_add_test(NAME level_gain SOURCES gain_test.cpp)
miciri_add_test(NAME level_signal_device SOURCES signal_device_test.cpp)
miciri_add_test(NAME level_noise SOURCES noise_test.cpp)
miciri_add_test(NAME level_hum SOURCES hum_test.cpp)
miciri_add_test(NAME level_channels SOURCES channels_test.cpp)
miciri_add_test(NAME level_analyze_cli SOURCES analyze_cli_test.cpp LINK miciri_analyze_lib)
miciri_add_test(NAME golden_dsp SOURCES golden_test.cpp LABELS golden
  DEFINITIONS $<$<BOOL:${MICIRI_UPDATE_GOLDEN}>:MICIRI_UPDATE_GOLDEN=1>)
