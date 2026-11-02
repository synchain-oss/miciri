#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# macOS 上的两道验证门，build-mac 调用：
#   mac.sh pluginval-install                      按 scripts/ci/mac/pluginval-macos.sha256 下载并核对 pluginval，路径写进 GITHUB_ENV
#   mac.sh pluginval <.vst3 或 .component>...      pluginval strictness 5，不开 GUI
#   mac.sh auval <.component>                     装进用户的 Components 目录，auval -v 通过，且 auval -a 里恰好出现一行
#   mac.sh --self-test                            判决函数的正反例，不调 auval
# AU 的身份：类型 aufx、子类型 Mcri、厂商 Snch（与插件码、厂商码一致）；CMake 改了身份时这里一起改。
# auval 的退出码历史上不可靠：输出里要有成功标记且退出码为 0，缺一不可。
set -euo pipefail

AU_TYPE=aufx
AU_SUB=Mcri
AU_MFR=Snch

# $1 = auval -v 的退出码，$2 = 输出文件。通过返回 0。
verdict() {
  [ "$1" = "0" ] && grep -q "AU VALIDATION SUCCEEDED" "$2"
}

# $1 = auval -a 的输出文件。恰好一行含 "<类型> <子类型> <厂商>" 时返回 0。
unique() {
  local n
  n=$(grep -c "^ *$AU_TYPE  *$AU_SUB  *$AU_MFR" "$1" || true)
  [ "$n" = "1" ]
}

self_test() {
  local d; d=$(mktemp -d); local ok=0
  printf 'AU VALIDATION SUCCEEDED.\n' > "$d/pass"
  printf 'FATAL ERROR\n' > "$d/fail"
  verdict 0 "$d/pass" || { echo "自测失败：成功输出加退出码 0 应通过"; ok=1; }
  verdict 1 "$d/pass" && { echo "自测失败：退出码非 0 应判红"; ok=1; }
  verdict 0 "$d/fail" && { echo "自测失败：没有成功标记应判红"; ok=1; }
  printf ' %s  %s  %s  -  Synchain: Miciri\n' "$AU_TYPE" "$AU_SUB" "$AU_MFR" > "$d/one"
  cat "$d/one" "$d/one" > "$d/two"
  : > "$d/none"
  unique "$d/one" || { echo "自测失败：恰好一行应通过"; ok=1; }
  unique "$d/two" && { echo "自测失败：两行应判红"; ok=1; }
  unique "$d/none" && { echo "自测失败：零行应判红"; ok=1; }
  rm -rf "$d" # miciri-delete-ok: 上面 mktemp 新建的临时目录
  [ "$ok" = "0" ] && echo "mac.sh 自测通过（坏样例 4、正例 2）"
  return "$ok"
}

case "${1:-}" in
  --self-test) self_test ;;
  pluginval-install)
    read -r tag sha < scripts/ci/mac/pluginval-macos.sha256
    dir="${RUNNER_TEMP:?}/pluginval"
    mkdir -p "$dir"
    curl -sSfL -o "$dir/pluginval.zip" "https://github.com/Tracktion/pluginval/releases/download/$tag/pluginval_macOS.zip"
    echo "$sha  $dir/pluginval.zip" | shasum -a 256 -c -
    unzip -q -o "$dir/pluginval.zip" -d "$dir"
    echo "MICIRI_PLUGINVAL=$dir/pluginval.app/Contents/MacOS/pluginval" >> "$GITHUB_ENV"
    ;;
  pluginval)
    shift
    [ "$#" -ge 1 ] || { echo "用法：mac.sh pluginval <bundle>..." >&2; exit 2; }
    for b in "$@"; do
      "${MICIRI_PLUGINVAL:?}" --strictness-level 5 --timeout-ms 60000 --skip-gui-tests "$b"
    done
    ;;
  auval)
    comp="${2:?用法：mac.sh auval <.component>}"
    dest="$HOME/Library/Audio/Plug-Ins/Components"
    mkdir -p "$dest"
    cp -R "$comp" "$dest/"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    out=$(mktemp)
    set +e
    auval -v "$AU_TYPE" "$AU_SUB" "$AU_MFR" > "$out" 2>&1
    code=$?
    set -e
    tail -n 20 "$out"
    verdict "$code" "$out" || { echo "::error::auval -v $AU_TYPE $AU_SUB $AU_MFR 没有通过（退出码 $code）"; exit 1; }
    auval -a > "$out" 2>&1 || true
    unique "$out" || { echo "::error::auval -a 里 $AU_TYPE $AU_SUB $AU_MFR 不是恰好一行"; exit 1; }
    ;;
  *) echo "用法见文件头注释" >&2; exit 2 ;;
esac
