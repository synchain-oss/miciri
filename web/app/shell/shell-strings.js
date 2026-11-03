// SPDX-License-Identifier: GPL-3.0-or-later
//
// 外壳自己的词条：页脚、关于窗、错误提示。步骤内容、测试面板和报告的词条由各自的页面带来。
// 界面文案全部走词条，不从原生拿句子；两个语言的键必须完全一致，占位符一致（冒烟测试核对）。

export default {
    zh: {
        "shell.hint": "纯直通，不改变你的声音",
        "shell.version": "Synchain {v}",
        "shell.lang.aria": "界面语言",
        "shell.scale.aria": "窗口缩放",
        "shell.help.aria": "使用指南",
        "shell.about.aria": "关于 Miciri",
        "shell.err.scaleLocked": "现在不能改窗口缩放，请稍后再试。",
        "shell.err.generic": "操作没有成功，请再试一次。",
        "about.title": "关于 Miciri",
        "about.body": "Miciri 帮你检查录音：增益、距离、方向、房间和噪声。它只测量，不改变你的声音，也不会发出声音。",
        "about.gpl": "Miciri 是自由软件，按 GNU 通用公共许可证（GPL）发布，不提供任何担保。",
        "about.contact": "联系",
        "about.license": "查看许可证",
    },
    en: {
        "shell.hint": "Pure pass-through. Your sound is not changed.",
        "shell.version": "Synchain {v}",
        "shell.lang.aria": "Language",
        "shell.scale.aria": "Window scale",
        "shell.help.aria": "Guide",
        "shell.about.aria": "About Miciri",
        "shell.err.scaleLocked": "The window scale cannot be changed right now. Please try again later.",
        "shell.err.generic": "That did not work. Please try again.",
        "about.title": "About Miciri",
        "about.body":
            "Miciri helps you check a recording: gain, distance, direction, room and noise. It only measures; it never changes your sound and makes no sound of its own.",
        "about.gpl": "Miciri is free software released under the GNU General Public License (GPL), with no warranty.",
        "about.contact": "Contact",
        "about.license": "View license",
    },
};
