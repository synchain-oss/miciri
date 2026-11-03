# SPDX-License-Identifier: GPL-3.0-or-later
#
# 插件测试：处理器的逐位直通与电平（不链 WebView），以及编辑器反复开关。
# 只建 core 的构建里没有 JUCE，整个目录跳过。
if(MICIRI_CORE_ONLY)
  return()
endif()

# JUCE 模块会给链接它的目标加 C 源文件，而根 project() 只开了 CXX，所以这里补开 C（见 src/plugin/CMakeLists.txt）。
enable_language(C)

set(_plugin_src "${CMAKE_SOURCE_DIR}/src/plugin")

# 处理器测试自己提供 createEditor 的空实现，所以不用链编辑器与 WebView。
miciri_add_test(NAME plugin_passthrough
  SOURCES passthrough_test.cpp EditorStub.cpp "${_plugin_src}/MiciriProcessor.cpp"
  LINK juce::juce_audio_processors)

# 编辑器开关：编辑器源码要自己的 WebView2 链接，所以用 juce_add_console_app 建目标，不走 miciri_add_test。
# 没有显示器（无法开窗）时测试自己 SKIP，退出码 4 登记为“跳过”而不是失败。
get_property(_ui_sources GLOBAL PROPERTY MICIRI_PLUGIN_UI_SOURCES)
# JUCE 前端桥脚本的资源测试：用与插件相同的暂存函数（src/plugin/CMakeLists.txt）把脚本编进测试自己的二进制数据，
# 再让 ResourceProvider 按页面路径取。它和编辑器开关共用这个可执行文件（同样需要 ResourceProvider 与 JUCE 的 gui_extra）。
miciri_stage_juce_frontend("${CMAKE_CURRENT_BINARY_DIR}/juce-frontend" _juce_frontend_file)
juce_add_binary_data(MiciriJuceFrontendTestData
  NAMESPACE MiciriJuceFrontendTestData
  HEADER_NAME MiciriJuceFrontendTestData.h
  SOURCES "${_juce_frontend_file}")

juce_add_console_app(plugin_editor_open_close
  PRODUCT_NAME "plugin_editor_open_close"
  NEEDS_WEB_BROWSER TRUE
  NEEDS_WEBVIEW2 TRUE)
target_sources(plugin_editor_open_close PRIVATE
  editor_open_close_test.cpp
  frontend_resource_test.cpp
  "${_plugin_src}/MiciriProcessor.cpp"
  ${_ui_sources})
target_include_directories(plugin_editor_open_close PRIVATE
  "${_plugin_src}" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/tests")
# 测试里要自己跑一会儿消息循环（runDispatchLoopUntil），需要放开模态循环。
target_compile_definitions(plugin_editor_open_close PRIVATE ${MICIRI_PLUGIN_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(plugin_editor_open_close PRIVATE
  miciri_core
  MiciriJuceFrontendTestData
  Catch2::Catch2WithMain
  juce::juce_audio_processors
  juce::juce_data_structures
  juce::juce_gui_extra)
miciri_target_warnings(plugin_editor_open_close PRIVATE)
miciri_warnings_as_errors(plugin_editor_open_close)
add_test(NAME plugin_editor_open_close COMMAND plugin_editor_open_close WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(plugin_editor_open_close PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 4)
