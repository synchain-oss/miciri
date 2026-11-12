# SPDX-License-Identifier: GPL-3.0-or-later
#
# 人声三项（喷麦、齿音与有效带宽、口水音）的测试：合成信号上的解析真值、增益与采样率不变性、
# 每条排除规则的正反例，以及锁定 measures 的 golden（期望值写在 voice_golden_test.cpp 里）。

miciri_add_test(NAME voice_plosive SOURCES plosive_test.cpp)
miciri_add_test(NAME voice_sibilance SOURCES sibilance_test.cpp)
miciri_add_test(NAME voice_clicks SOURCES clicks_test.cpp)
miciri_add_test(NAME voice_golden SOURCES voice_golden_test.cpp LABELS golden)
