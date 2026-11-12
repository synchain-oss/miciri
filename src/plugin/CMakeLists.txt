# SPDX-License-Identifier: GPL-3.0-or-later
#
# 插件目标 Miciri（VST3，macOS 上再加 AU）。依赖 JUCE，只建 core 的构建里整个跳过。
# 身份变量（公司、厂商码、插件码、bundle、格式、编译定义）在根 CMakeLists.txt 里定义，这里只读。
#
# 页面资源：配置期把 web/ 下的 app、shared、i18n、fonts 按相对路径复制成扁平文件名（斜杠换双下划线）
# 放进构建目录，再经 juce_add_binary_data 编进二进制；ResourceProvider 用同样的映射查找。
# 扁平化是为了不同目录下的同名文件（如多个 index.html）不会在二进制里撞名。
# web/ 里没有任何资源时不建资源目标，编辑器运行时显示兜底面板。
if(MICIRI_CORE_ONLY)
  return()
endif()

# JUCE 的插件格式封装里有 C 源文件，而根 CMakeLists.txt 的 project() 只开了 CXX；这里补开 C，
# 否则生成阶段报 CMAKE_C_COMPILE_OBJECT 未设置。
enable_language(C)

# 测试构建：插件自己把计数（离线块数、全 0 块数、编辑器开关次数等）写成 JSON，给外部的宿主测试脚本读。
# 默认 OFF，发布版必须是 OFF：OFF 时探针的源文件不编进插件，二进制里没有探针代码，也没有环境变量名。说明见 docs/HOST_PROBE.md。
option(MICIRI_HOST_PROBE "测试构建：写宿主探针 JSON（仅供宿主测试，不得发布）" OFF)

include("${CMAKE_CURRENT_LIST_DIR}/MiciriWebView2.cmake")

string(REPLACE "|" ";" _miciri_vst3_categories "${MICIRI_VST3_CATEGORY}")

juce_add_plugin(Miciri
  PRODUCT_NAME "Miciri"
  COMPANY_NAME "${MICIRI_COMPANY}"
  COMPANY_COPYRIGHT "Copyright 2026 Synchain"
  COMPANY_WEBSITE "https://synchain.ca"
  COMPANY_EMAIL "contact@synchain.ca"
  BUNDLE_ID "${MICIRI_BUNDLE_ID}"
  PLUGIN_MANUFACTURER_CODE ${MICIRI_MANUFACTURER_CODE}
  PLUGIN_CODE ${MICIRI_PLUGIN_CODE}
  FORMATS ${MICIRI_PLUGIN_FORMATS}
  VST3_CATEGORIES ${_miciri_vst3_categories}
  AU_MAIN_TYPE kAudioUnitType_Effect
  AU_SANDBOX_SAFE FALSE
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  NEEDS_WEB_BROWSER TRUE
  NEEDS_WEBVIEW2 TRUE)

# 编辑器一侧的源码清单：编辑器开关测试也编这些（它要自己的 WebView2 链接）。
set(MICIRI_PLUGIN_UI_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/BridgeBase.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/FallbackPanel.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/MiciriEditor.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/PlatformWebView.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/PlatformWebViewRuntime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/ResourceProvider.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/UiDefaultsStore.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/WebViewHost.cpp")
set_property(GLOBAL PROPERTY MICIRI_PLUGIN_UI_SOURCES "${MICIRI_PLUGIN_UI_SOURCES}")

target_sources(Miciri PRIVATE
  ${MICIRI_PLUGIN_UI_SOURCES}
  "${CMAKE_CURRENT_LIST_DIR}/MiciriProcessor.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/PluginEntry.cpp")
target_include_directories(Miciri PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(Miciri PRIVATE
  miciri_core
  miciri_session
  juce::juce_audio_processors
  juce::juce_data_structures
  juce::juce_gui_extra)
target_compile_definitions(Miciri PUBLIC ${MICIRI_PLUGIN_DEFINITIONS})
if(MICIRI_HOST_PROBE)
  message(STATUS "MICIRI_HOST_PROBE=ON：这是宿主测试构建，不要发布")
  target_sources(Miciri PRIVATE "${CMAKE_CURRENT_LIST_DIR}/HostProbe.cpp")
  target_compile_definitions(Miciri PRIVATE MICIRI_HOST_PROBE=1)
endif()

# JUCE 的前端桥脚本：页面里的 bridge.js 在真实宿主中按页面根下的 js/juce/index.js 取它。文件不进本仓，
# 配置期从 JUCE 检出里复制到构建目录，按同样的扁平名编进二进制，ResourceProvider 照常按页面路径提供。
# stage_dir 是构建目录里的暂存目录，复制后的文件路径写进 out_var。测试也用这个函数，保证两边是同一份映射。
# 常量放在函数里：函数在调用者的作用域里运行，tests/ 下调用时看不到本目录的变量。
function(miciri_stage_juce_frontend stage_dir out_var)
  set(_rel "modules/juce_gui_extra/native/javascript/index.js")
  set(_page_path "js/juce/index.js")
  set(_src "${_miciri_juce_dir}/${_rel}")
  if(NOT EXISTS "${_src}" OR IS_DIRECTORY "${_src}")
    message(FATAL_ERROR "找不到 JUCE 的前端桥脚本 ${_rel}（JUCE_PATH 要指向 JUCE 8.0.8 的检出）")
  endif()
  string(REPLACE "/" "__" _flat "${_page_path}")
  configure_file("${_src}" "${stage_dir}/${_flat}" COPYONLY)
  set(${out_var} "${stage_dir}/${_flat}" PARENT_SCOPE)
endfunction()

set(_web_root "${CMAKE_SOURCE_DIR}/web")
set(_web_stage "${CMAKE_CURRENT_BINARY_DIR}/web-assets")
file(GLOB_RECURSE _web_files CONFIGURE_DEPENDS RELATIVE "${_web_root}"
  "${_web_root}/app/*" "${_web_root}/shared/*" "${_web_root}/i18n/*" "${_web_root}/fonts/*")
set(_web_staged)
foreach(_rel IN LISTS _web_files)
  if(NOT _rel MATCHES "\\.(html|js|mjs|css|json|png|svg|woff2)$" OR _rel MATCHES "\\.(test|spec)\\.")
    continue()
  endif()
  string(REPLACE "/" "__" _flat "${_rel}")
  configure_file("${_web_root}/${_rel}" "${_web_stage}/${_flat}" COPYONLY)
  list(APPEND _web_staged "${_web_stage}/${_flat}")
endforeach()
if(_web_staged)
  miciri_stage_juce_frontend("${_web_stage}" _juce_frontend)
  list(APPEND _web_staged "${_juce_frontend}")
  juce_add_binary_data(MiciriWebData
    NAMESPACE MiciriWebData
    HEADER_NAME MiciriWebData.h
    SOURCES ${_web_staged})
  target_link_libraries(Miciri PRIVATE MiciriWebData)
  target_compile_definitions(Miciri PRIVATE MICIRI_HAVE_WEB_ASSETS=1)
endif()

miciri_target_warnings(Miciri PRIVATE)
miciri_warnings_as_errors(Miciri)
