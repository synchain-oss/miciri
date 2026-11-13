// SPDX-License-Identifier: GPL-3.0-or-later
//
// 应用内指南的词条：七步的标题与说明、翻页按钮、提示。文案以已定稿的设计为准；
// 凡是阈值、时长一律不写进词条（目标区、确认时限由界面自己的词条和快照负责）。

export default {
    zh: {
        "guide.hint": "点任意处继续 · Esc 跳过 · ← → 翻页",
        "guide.step": "{n} / {total}",
        "guide.prev": "上一步",
        "guide.next": "下一步",
        "guide.skip": "跳过",
        "guide.done": "完成",
        "guide.input.t": "输入卡：随时看得见",
        "guide.input.b": "不管做到哪一步，左边这张卡都在。它显示插件收到的声音，不会改变它。",
        "guide.meter.t": "电平表",
        "guide.meter.b":
            "管子显示输入电平，立体声轨时每路一根。浅绿色是目标区，白线是最近的峰值，圆灯亮了说明削波过，点一下清除。",
        "guide.mic.t": "话筒类型",
        "guide.mic.b": "选一个最像你的话筒；拿不准就选“不确定”。它只影响给你的建议，不影响测量。",
        "guide.main.t": "这里看结果",
        "guide.main.b":
            "每一步的结果出现在这块区域：一个词、一个数，再加一条区间。上下露出的是前后两步，滚轮或点一下就能翻过去。",
        "guide.cta.t": "开始测试",
        "guide.cta.b": "按下后整个窗口会变成一块大色块，隔着一段距离也看得清。随时可以点取消。",
        "guide.scale.t": "窗口大小",
        "guide.scale.b": "窗口太小或太大就改这里。选完会问你要不要保留，没确认就会自动退回原样。",
        "guide.help.t": "再看一遍",
        "guide.help.b": "点这个圆形“?”可以重新看这份引导。",
    },
    en: {
        "guide.hint": "Click anywhere to continue · Esc to skip · ← → to turn pages",
        "guide.step": "{n} / {total}",
        "guide.prev": "Back",
        "guide.next": "Next",
        "guide.skip": "Skip",
        "guide.done": "Done",
        "guide.input.t": "Input card: always in view",
        "guide.input.b":
            "Whatever step you are on, this card stays on the left. It shows the sound the plugin receives and never changes it.",
        "guide.meter.t": "Level meter",
        "guide.meter.b":
            "The tube shows the input level, one per channel on a stereo track. The light-green band is the target zone, the white line is the recent peak, and the round lamp lights if the signal clipped; click it to clear.",
        "guide.mic.t": "Mic type",
        "guide.mic.b":
            "Pick the type closest to your mic, or “Not sure”. It only changes the advice, not the measurement.",
        "guide.main.t": "Results show here",
        "guide.main.b":
            "Each step shows its result in this area: one word, one number and a range. The cards above and below are the neighboring steps; scroll or click one to turn the page.",
        "guide.cta.t": "Start a test",
        "guide.cta.b":
            "The whole window turns into one big block of color, readable from a distance. You can cancel at any time.",
        "guide.scale.t": "Window size",
        "guide.scale.b":
            "Change the window size here. After you pick one you are asked whether to keep it; if you do not answer, it goes back by itself.",
        "guide.help.t": "See it again",
        "guide.help.b": "Press this round “?” to replay the guide.",
    },
};
