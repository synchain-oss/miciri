// SPDX-License-Identifier: GPL-3.0-or-later
//
// 共享件用到的词条：缩放确认与展开菜单。键必须与 scale-ui.js 的 SCALE_KEYS 一致。
// 各页面的词条放在页面自己的目录，合并时键重名会抛错，所以共享件的键统一以 common. 开头。

export default {
    zh: {
        "common.cancel": "取消",
        "common.close": "关闭",
        "common.scale.title": "窗口缩放",
        "common.scale.overflow": "超出当前屏幕",
        "common.scale.confirmBody": "窗口已按新档位缩放。{s} 秒内不保存，会自动回到原来的档位。",
        "common.scale.keep": "保存",
        "common.scale.lockedLegacy": "当前系统版本只支持 100% 缩放",
    },
    en: {
        "common.cancel": "Cancel",
        "common.close": "Close",
        "common.scale.title": "Window scale",
        "common.scale.overflow": "Larger than this screen",
        "common.scale.confirmBody":
            "The window now uses the new scale. If you do not keep it within {s} seconds, it returns to the previous one.",
        "common.scale.keep": "Keep",
        "common.scale.lockedLegacy": "This system version only supports 100% scale",
    },
};
