# SPDX-License-Identifier: GPL-3.0-or-later
# Derived from synchain-oss/rinne; see upstream/plugin.json
#
# Windows 上 WebView2 的 NuGet 包：JUCE 的 NEEDS_WEBVIEW2 只在本机找已经装好的包、链接里面的静态 loader，不负责下载。
# 这里在配置期把钉版的包从 nuget.org 取到构建目录，按 sha256 核对后解开，再让 JUCE 去那里找，不依赖 nuget.exe。
# 变量写进缓存：测试目录晚于本目录配置，也要看得到。

if(NOT WIN32)
  return()
endif()

set(MICIRI_WEBVIEW2_VERSION "1.0.2957.106")
set(MICIRI_WEBVIEW2_SHA256 "4c35a54835b63954159eac1d5b7a60ae617a41dbb5b73bfdb11c4870a891080a")

set(_wv2_root "${CMAKE_BINARY_DIR}/packages")
set(_wv2_dir "${_wv2_root}/Microsoft.Web.WebView2")
set(_wv2_stamp "${_wv2_dir}/.miciri-package")
set(_wv2_want "${MICIRI_WEBVIEW2_VERSION} ${MICIRI_WEBVIEW2_SHA256}")

set(_wv2_have "")
if(EXISTS "${_wv2_stamp}")
  file(READ "${_wv2_stamp}" _wv2_have)
endif()

if(NOT _wv2_have STREQUAL _wv2_want)
  # 下载放在 .download/ 里：JUCE 只看 packages/ 下一层名字带 Microsoft.Web.WebView2 的条目，多一个就告警。
  set(_wv2_pkg "${_wv2_root}/.download/Microsoft.Web.WebView2.${MICIRI_WEBVIEW2_VERSION}.nupkg")
  message(STATUS "下载 Microsoft.Web.WebView2 ${MICIRI_WEBVIEW2_VERSION}")
  file(DOWNLOAD "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${MICIRI_WEBVIEW2_VERSION}" "${_wv2_pkg}"
    EXPECTED_HASH SHA256=${MICIRI_WEBVIEW2_SHA256}
    TLS_VERIFY ON
    STATUS _wv2_status)
  list(GET _wv2_status 0 _wv2_rc)
  if(NOT _wv2_rc EQUAL 0)
    message(FATAL_ERROR "下载 WebView2 包失败：${_wv2_status}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${_wv2_pkg}" DESTINATION "${_wv2_dir}" PATTERNS "build/native/*")
  file(REMOVE "${_wv2_pkg}")
  if(NOT EXISTS "${_wv2_dir}/build/native/include/WebView2.h")
    message(FATAL_ERROR "WebView2 包解开后缺 build/native/include/WebView2.h")
  endif()
  file(WRITE "${_wv2_stamp}" "${_wv2_want}")
endif()

set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${_wv2_root}" CACHE PATH "WebView2 NuGet 包所在目录" FORCE)
