# SPDX-License-Identifier: GPL-3.0-or-later
#
# Session 的测试：各步的编排、过期规则与快照形状（不起线程）；证据波形（不起线程）；采集环、工作线程与保护（起线程，TSan 覆盖这些交接）。

miciri_add_test(NAME session_steps SOURCES steps_test.cpp LINK miciri_session)
miciri_add_test(NAME session_threads SOURCES session_test.cpp LINK miciri_session)
miciri_add_test(NAME session_evidence SOURCES evidence_test.cpp LINK miciri_session)
