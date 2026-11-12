# SPDX-License-Identifier: GPL-3.0-or-later
#
# 房间、位置、方向与合成判断的测试：解析真值（已知 T60 的衰减噪声、已知 D/R）、门限与重复性、决策表每一格。

miciri_add_test(NAME room_reverb SOURCES room_test.cpp)
miciri_add_test(NAME room_decide SOURCES decide_test.cpp)
