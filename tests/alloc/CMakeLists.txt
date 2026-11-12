# SPDX-License-Identifier: GPL-3.0-or-later
#
# 分配计数器：全局 operator new 的钩子，在音频线程路径（processBlock）上必须一次分配都没有。
# 带标签 alloc，scripts/build.ps1 的分配计数器一项按标签跑它。只建 core 的构建里没有 JUCE，跳过。
if(MICIRI_CORE_ONLY)
  return()
endif()

# JUCE 模块会给链接它的目标加 C 源文件，而根 project() 只开了 CXX，所以这里补开 C（见 src/plugin/CMakeLists.txt）。
enable_language(C)

miciri_add_test(NAME alloc_processblock
  SOURCES alloc_test.cpp "${CMAKE_SOURCE_DIR}/tests/plugin/EditorStub.cpp" "${CMAKE_SOURCE_DIR}/src/plugin/MiciriProcessor.cpp"
  LINK miciri_session juce::juce_audio_processors
  LABELS alloc)
