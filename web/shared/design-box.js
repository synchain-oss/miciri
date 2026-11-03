// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/web.json
//
// design-box.js：设计盒常量的唯一真源。
//
//   shell 1120×700：页眉、左栏、主区、页脚按这个盒子摆放，窗口缩放只改整体倍率。
//   hostFrame 是宿主给插件窗加的框（标题栏加工具条）的估计值，上机实测后再调；
//   它只用来判断某个缩放档位在当前屏幕上放不放得下：
//   W*F + hostFrame.w > availWidth 或 H*F + hostFrame.h > availHeight 的档位置灰并提示「超出当前屏幕」。
//
//   纪律一：任何位置（C++、HTML、CSS、预览页）都不得二次硬编码这些数字；CSS 里的 --design-w、--design-h
//   由冒烟测试与本文件逐值对拍，改值要两处一起改。
//   纪律二：本文件保持「单个对象字面量加纯数字」的形状，不引入运算、依赖或条件分支，方便静态读取。
//   缩放档位是设计值，不是可用值：运行时还要按屏幕可用区过滤。

export const DESIGN = {
    shell: { w: 1120, h: 700, presets: [0.5, 0.65, 0.8, 1, 1.25, 1.5, 2] },
    hostFrame: { w: 16, h: 39 },
};
