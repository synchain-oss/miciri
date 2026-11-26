<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 算法与阈值

本文说明 Miciri 的每一项读数怎么得出来，判定用到哪些阈值，哪些阈值有实录依据、哪些还只是起始值，以及现有样本的局限。
契约在 `docs/contracts/`（`RESULT.md`、`THRESHOLDS.md`、`BRIDGE.md`），阈值的数据在仓根 `thresholds.json`；
本文末尾的阈值表由它生成，不要手改（`node scripts/gen-algorithms.mjs` 重新生成，检查 `algorithms-doc` 核对）。

## 先看这几条

- **只分析，不改声音。** 音频逐位直通，分析在副本上、工作线程里做。`src/core/` 不依赖 JUCE，全是纯函数，double 精度。
- **连续量与档位分开。** 每项结果的 `measures` 是连续量（dB、Hz、秒、次数、比值），DSP 的 golden 只锁它；档位 `verdict` 由连续量加阈值得出，改阈值只动档位。
- **每项结果带置信度。** `trusted` 或 `retest`（并给原因）。要再测的结果没有结论：卡片无色，也不进“最该改”。
- **有几项不分档。** D/R、喷麦、齿音、口水音只给数值与重测前后的“上次 → 现在”，卡片无色。喷麦、齿音、口水音各给一个参考值，
  超过时在自己的卡上给一条轻提示。参考值是经验值，不是档界，也不是“合格线”。
- **阈值只放 `thresholds.json`。** 代码里不写魔法数；每一项写明状态、依据和日期。

## 阈值的依据，与样本的局限

### 三种状态

| 状态 | 含义 |
|---|---|
| 合成验证（`synthetic`） | 在合成信号上核对过。它说明算法的实现对不对，不说明这个数在真实录音上合不合适 |
| 待标定（`pending`） | 规格或工程里的起始值，或者还没有值（`null`）。等实录标定 |
| 实录标定（`calibrated`） | 在实录上核对过。`source` 写依据，也写还缺哪一侧的录音 |

合成验证不等于实录标定：所有合成验证的值还要在实录上复核。表里每一项的“依据”列写了它是什么口径，实录标定的项还写了还缺什么。

实录标定也不表示两侧都有录音。例如啸叫的频率上限（`whistle_max_hz`）与电平下限（`whistle_min_dbfs`），现在只被安静录音里的假窄峰（会误报的转换器与时钟音）约束，
还没有真啸叫的录音；电流声的“轻微”界线同样缺响的电流声一侧。读阈值表时以每一项“依据”里写的“还缺”为准。

### 现有的实录样本

目前的实录标定来自 2026 年 11 月的一批录音：**3 间房、2 位说话人、2 支话筒**。能从它得出什么、不能得出什么：

- **房间。** 三间房里有两间的 T20 都约 0.29 s（落在“正常”档），另一间约 0.58 s（“偏混”档）；没有很干的房间，也没有很大的空间。
  T20 的测量在这些房间上达标，但干、正常、偏混、很混四档的档界都没有实录依据，仍是规格起始值。
- **说话人与话筒。** 两位说话人各用一支话筒，说话人和话筒是一一绑定的，分不开“人的差别”和“话筒的差别”。
  没有同一个人用两支话筒的对照，也没有第三位说话人来检验一个阈值是不是只适合其中一位。
- **D/R。** 随距离单调，但 T20 与 D/R 合起来的决策表在这批样本上只判对 10 个里的 6 个，所以 D/R 不分档，只用于重测位置之后的前后对比。
- **喷麦。** 同一个阈值只在一位说话人身上能分开近与远，所以不分档，只给数值、参考值与前后对比。
- **齿音。** 绝对电平随挪远的方向一致，偏轴 15° 只差 1 到 2 dB，所以没有“偏轴再测、比一比”这一步，前后对比只用固定增益下的绝对电平；
  对元音的比值随距离的方向不稳（近讲效应，挪远后反而可能变大），只作参考。
- **参考值。** 喷麦 9 dB、齿音 −3 dB、口水音每分钟 75 次，都是经验值，不是档界，也不是合格线，超过不等于有问题。各自的依据：
  - 喷麦 9 dB：两位说话人在 5 cm 处，每遍字表的最大值都超过它，在 60 cm 处都低于它；一位在 15 cm 处有一遍超过。
  - 齿音 −3 dB：常见做法的默认；同一批录音里两位说话人在唱歌位置（约 15 cm）的中位比值是 −6.8 与 −8.4 dB，都低于 −3 dB。
  - 口水音每分钟 75 次：两位说话人在约 15 cm 处的自然读数都低于它，一位刻意咂嘴时高于它，另一位刻意咂嘴时与正常读数相近；检出里仍有送气音（t、k）的误报。
- **推荐距离。** 静态的常见做法（电容与 USB 约 15 cm，动圈约 5 到 10 cm），不是测量，也不是这批样本的结论。

还缺的录音包括：很干与很大的房间、更多的说话人与话筒类型、真正响的电流声与真正的啸叫。每一项缺什么，见阈值表里它自己的“依据”。

## 各项测量

### 无信号与声道检查

- **无信号。** 按 100 ms 帧看电平：没有回调（编辑器的定时器按墙钟判断，不进分析）、从一开始全零、之前有信号后来全零、
  极低且说话时电平不上升。文案一律写“可能”，因为插件看不到宿主的监听与布防状态。
- **声道检查。** 只在立体声轨上做，不拦测试。20 ms 帧，每路底噪取帧功率的 P5；“差”（L−R）与“和”（L+R）都和两路底噪之和比。
  按这个顺序判：一路全零、一路明显更响、两路相位相反、两路相同、真立体声。分析哪一路由界面选的声道给出，不把两路相加。

### 增益与削波

- 峰值取**真峰值**（过采样后取最大；96 kHz 以内 4 倍，更高 2 倍），响度按 BS.1770：K 加权，400 ms 块、100 ms 跳，绝对门限与相对门限。
- **数字削波**：连续样本达到削波阈值，并且连续至少 `max(最少样本数, 最短时长 × 采样率)`。**模拟平顶**另计：只看高出全局峰值一定比例的片段，
  平台的容差随底噪的标准差放宽，样本峰值太低的声道不查。数字削波红并点亮削波灯；平顶最多到琥珀的“可能削波”。
- **档位**由真峰值与四个界（过低、偏低、偏高、过高）比出；目标峰值用来写“把输入音量调多少 dB”。

### 噪声

A 加权后 100 ms 帧功率的 P10 作底噪；A 加权 SNR（说话时的活跃电平减底噪）是唯一的分档依据，全频电平只作信息。
活跃帧是落在第 95 百分位帧电平以内一定 dB、且高出底噪帧 P10 一定 dB 的帧。采样率高于分析上限时先整数倍抽取。

### 电流声与啸叫

Welch 谱（2 秒汉宁窗、50% 重叠）里找市电基频（50 或 60 Hz）的谐波：先由最强谐波精确估 f0，再逐个谐波查“峰比 ±5 Hz 局部中值高”
且“多数段里都在”；前 10 次谐波里至少 3 个稳定才判电流声；检出的谐波延伸到 1 kHz 以上判嗡嗡声。
最强谐波高出本底不到阈值、或电流声电平低于阈值的只标“轻微”，轻微不进“最该改”。2 kHz 到上限之间、绝对电平够高的稳定窄峰判啸叫。

### 设备处理

只作提示横幅：录音中间有没有一段严格的数字零（噪声门的迹象，首尾的零不算），以及样本是否都落在 16 bit 的量化格点上（处理链的迹象）。
底噪低于误报下限，或处在“极低不上升”状态时不显示。不进“最该改”。

### 房间：T20

拍手算。两项共用同一次拍手录音：逐块收样本，找拍手（只看有起音的样本，每次取剩下的最响处，抹掉它前后各半个最小间隔）。
每一下在 500、1k、2k、4k 四个倍频带里各自判断：带通后的能量减去拍手前的噪声，Schroeder 反向积分，衰减降到“噪声加余量”时截断，
在 −5 到 −25 dB 段线性拟合。一下一个频带有效，要同时满足峰噪比够、拟合 R² 够、没有门限；门限只作废那一下的那个频带。
每个频带去掉偏离中位数太多的几下，剩下的够多才给值，取中位数。主指标 T20 是 500 与 1k 的平均，两个频带都要有值；直达声削波不影响 T20。
档位：干、正常、偏混、很混，界在 `reverb` 组。房间建议只看这一项。

### 位置：D/R、喷麦、齿音、口水音与有效带宽

- **D/R**：1 到 4 kHz 带，直达窗内的能量比上其后到截断点的能量，两者都减去噪声；只用没削波的几下，取中位数；
  尾部高出噪声不够时这一下只算下限（真值只会更高）。不分档。
- **喷麦**：按宽带起音切出音节，量起音段的低频峰值比同音节元音稳态的低频电平高多少（同音节瞬态比）。一遍字表取所有音节里最大的那个，
  不取中位数：不喷的字（t、k 开头）会把中位数稀释掉。不分档。包络帧短于基频周期时帧功率随基频起伏，帧距取得很密，
  最大值不随分析起点变化。起音之后要跟着真的元音才算一个音节：元音稳态在 200 到 4000 Hz 的电平要比整段字表录音同频带的底噪
  高出 `vowel_over_floor_db`。字表念完碰到话筒架这类低频闷响，后面只有底噪，不算。
- **齿音**：按谱重心、平坦度与高频能量找擦音段，量 3 到 6 kHz 与 6 到 12 kHz 减去底噪后的绝对电平（dBFS），供同一增益下两次测试之间的比较。
  另给 6 到 12 kHz 对紧随其后的元音（200 到 4000 Hz）的比值，用来和参考值比；有效带宽不足 10 kHz 时高频项关闭。不分档。
  谱帧互相重叠、帧距很密，读数不随分析起点变化。
- **有效带宽**：从起始频率起每 500 Hz 一个频点，看以它为中心 1 kHz 宽的频带，说话高出底噪 10 dB 的最高频点；
  底噪取带内帧功率的第 2 百分位（数字静音的帧不算）；连续 2 个频点不满足才停，所以一个窄谷不会让它提前停下。读数是 500 Hz 的整数倍。
  不按能量占比算：人声能量大多在低频，按占比会误关高频项。
- **口水音**：LPC 残差里超过局部尺度很多倍的尖峰；之后高频持续的（擦音）、之后低频和元音一起上来的（爆破）、高频背景本来就响的（齿音起头、送气）都不算。
  预测系数在相邻两块之间逐样本插值，局部尺度与语音电平用互相重叠的块，读数不随分析起点变化。
  只作“可能有口水音”的提示，不作距离证据。

三项的 `severity` 停在最低档只表示“未分档”，不表示“良好”，上层不要拿它直接着色。

### 距离：第一次的弱提示与重测后的前后对比

距离由别的结果合成，不给绝对判断，也不进“最该改”。

- **第一次**只有一条弱提示 `maybe-too-far`：当前、可信的噪声档位为差，且有当前、可信的 T20。给的建议只有“靠近一点”。
- **重测位置之后**，把 D/R、喷麦、齿音的“现在减上次”各和自己的分辨力比：超过算一个方向的迹象，不超过算没有明显变化。
  方向不一致是 `conflicting`（说不准）。D/R 只知道下限的一边真值只会更高：上次是下限时“更近”说不出，这次是下限时“更远”说不出。
- **不比的情形**：两次之间调过增益或声道布局变了，齿音不比（它给的是固定增益下的绝对电平）；换过房间，三项都不比。
- 三个分辨力都是实录里同一位置的散布与能分辨的差，仍待标定（见 `distance` 组）。
- `empiricalLimitM` 是按 T20 估的经验距离上限，只供文案使用，界面不显示。

### 方向

只回答“当前方向是不是最响”，不猜话筒型号。每个方向念一段：K 加权后分帧（帧互相重叠，读数不随分析起点变化），活跃帧是落在第 95 百分位帧电平一定 dB 以内的帧，电平取活跃帧电平的中位数。
亮度（3 到 10 kHz 对 300 到 1000 Hz）只显示，不参与判断：从背面进来的主要是房间声，亮度反而可能比正面高。
四个方向最响与最轻差不到一定 dB 说“差别不大”；当前方向就是最响的说“最响”；别的方向比当前响一定 dB 以上时重测那一对，两次都成立才说“可能放反”。没测方向不算问题。

### “最该改”的排序

无信号与数字削波固定在前；其余候选（方向、房间、电流声、增益、宽带噪声）按 影响 × 置信度 ÷ 修改代价 排序，低置信不进候选，过期的结果也不进。
影响与代价的数在 `priority` 组。距离与喷麦不在候选里。

## 阈值表

阈值表按 `thresholds.json` 的分组和先后排。“依据”列是各项自己的 `source`，开头的固定词（合成验证、规格起始值、工程起始值、产品约定、经验值、实录标定等）说明它的口径；
“说明”列是各项自己的 `note`，照 `thresholds.json` 的原文，多数是英文。单位写在名称的后缀里（`_s`、`_ms`、`_db`、`_dbfs`、`_hz` 等）。

<!-- prettier-ignore-start -->
<!-- thresholds:begin 以下由 scripts/gen-algorithms.mjs 按 thresholds.json 生成，不要手改 -->
共 301 项，分 17 组：合成验证 108 项、待标定 187 项、实录标定 6 项。

### `no_signal`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `no_callback_s` | 2 | 待标定 | 规格起始值 | wall clock, the only wall-clock threshold; read by the editor timer |
| `strict_zero_window_s` | 1 | 待标定 | 规格起始值 |  |
| `low_flat_window_s` | 6 | 合成验证 2026-11-02 | 合成验证 | observation window after the user is asked to speak |
| `frame_ms` | 100 | 合成验证 2026-11-02 | 合成验证 |  |
| `abs_dbfs` | -70 | 合成验证 2026-11-02 | 合成验证 | absolute level still needs real interface noise floors |
| `rise_db` | 6 | 合成验证 2026-11-02 | 合成验证 | max frame minus P10 of non-zero frames |
| `frame_percentile` | 10 | 合成验证 2026-11-02 | 合成验证 | of the non-zero frame power; the same P10 the rise is measured against |

### `channel_check`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `frame_ms` | 20 | 合成验证 2026-11-02 | 合成验证 |  |
| `floor_percentile` | 5 | 合成验证 2026-11-02 | 合成验证 | per channel, over pause frames |
| `floor_min_pause_s` | 1 | 待标定 | 规格起始值 |  |
| `active_over_floor_db` | 10 | 合成验证 2026-11-02 | 合成验证 |  |
| `min_active_s` | 0.5 | 待标定 | 规格起始值 |  |
| `one_quiet_delta_db` | 20 | 合成验证 2026-11-02 | 合成验证 |  |
| `one_quiet_weak_rise_db` | 3 | 合成验证 2026-11-02 | 合成验证 |  |
| `one_sided_delta_db` | 40 | 合成验证 2026-11-02 | 合成验证 | catches crosstalk when the converter is very quiet |
| `ident_diff_over_floor_db` | 3 | 合成验证 2026-11-02 | 合成验证 |  |
| `ident_rel_db` | -30 | 合成验证 2026-11-02 | 合成验证 |  |
| `ident_fallback_rel_db` | -40 | 合成验证 2026-11-02 | 合成验证 | no pause available |
| `anti_sum_over_floor_db` | 3 | 合成验证 2026-11-02 | 合成验证 |  |
| `anti_diff_over_floor_db` | 10 | 合成验证 2026-11-02 | 合成验证 |  |
| `anti_fallback_sum_rel_db` | -40 | 合成验证 2026-11-02 | 合成验证 | no pause available |
| `anti_fallback_diff_rel_db` | -20 | 待标定 | 规格起始值 | no pause available |
| `steady_hi_percentile` | 90 | 合成验证 2026-11-02 | 合成验证 | picks the louder channel as the reference and tests for a steady signal |
| `steady_lo_percentile` | 10 | 合成验证 2026-11-02 | 合成验证 |  |
| `steady_max_spread_db` | 10 | 合成验证 2026-11-02 | 合成验证 | hi percentile minus lo percentile below this: no pauses, the signal is continuous |
| `floor_valid_below_active_db` | 20 | 合成验证 2026-11-02 | 合成验证 | a floor reading counts only when it sits this far under the active level |

### `gain`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `true_peak_oversample_le_96k` | 4 | 合成验证 2026-11-02 | 合成验证 |  |
| `true_peak_oversample_gt_96k` | 2 | 合成验证 2026-11-02 | 合成验证 |  |
| `true_peak_rate_split_hz` | 96000 | 合成验证 2026-11-02 | 合成验证 |  |
| `clip_digital_threshold` | 0.999 | 合成验证 2026-11-02 | 合成验证 |  |
| `clip_digital_min_run_us` | 62.5 | 合成验证 2026-11-02 | 合成验证 | min run = max(clip_digital_min_run_samples, round(us * fs)) |
| `clip_digital_min_run_samples` | 3 | 合成验证 2026-11-02 | 合成验证 |  |
| `flat_tol_rel` | 0.0005 | 合成验证 2026-11-02 | 合成验证 | relative to the plateau median |
| `flat_min_ms` | 0.1 | 合成验证 2026-11-02 | 合成验证 |  |
| `flat_min_samples` | 3 | 合成验证 2026-11-02 | 合成验证 |  |
| `flat_min_peaks` | 3 | 合成验证 2026-11-02 | 合成验证 |  |
| `flat_min_peak_dbfs` | -12 | 实录标定 2026-11-21 | 实录标定（2026-11）：不设下限时误报的干净录音峰值最高 −15.2 dBFS，真正削波的接近 0 dBFS，在 −12 到 −3 之间取值两批结果相同（命中 6、误报 0）；还缺峰值在 −12 到 −3 dBFS 且带平顶的干净录音 | a channel whose sample peak is below this is not checked for flat tops; in a real take a quiet steady sweep lined its peaks up inside the noise tolerance |
| `flat_level_floor_frac` | 0.5 | 待标定 | 规格起始值 | only excursions above this fraction of the global peak are examined |
| `flat_top_cap_frac` | 0.01 | 合成验证 2026-11-02 | 合成验证 |  |
| `flat_sigma_k` | 3 | 合成验证 2026-11-02 | 合成验证 | tolerance = max(flat_tol_rel * level, k * sigma of the noise floor) |
| `flat_knee_drop_frac` | 0.01 | 合成验证 2026-11-02 | 合成验证 |  |
| `flat_knee_within_ms` | 0.1 | 合成验证 2026-11-02 | 合成验证 |  |
| `tp_too_quiet_below_dbfs` | -18 | 待标定 | 规格起始值 |  |
| `tp_quiet_below_dbfs` | -12 | 待标定 | 规格起始值 |  |
| `tp_loud_above_dbfs` | -6 | 待标定 | 规格起始值 |  |
| `tp_too_loud_above_dbfs` | -3 | 待标定 | 规格起始值 |  |
| `target_peak_dbfs` | -9 | 待标定 | 规格起始值 |  |
| `clip_advice_drop_db` | 6 | 待标定 | 规格起始值 |  |
| `flat_cap_sigma_k` | 4 | 合成验证 2026-11-02 | 合成验证 | the plateau cap window is max(flat_top_cap_frac * level, k * sigma) |
| `loudness_block_ms` | 400 | 合成验证 2026-11-02 | 合成验证 | gated loudness block length |
| `loudness_hop_ms` | 100 | 合成验证 2026-11-02 | 合成验证 | gated loudness hop; the block must be a whole number of hops |
| `loudness_abs_gate_lufs` | -70 | 合成验证 2026-11-02 | 合成验证 |  |
| `loudness_rel_gate_db` | -10 | 合成验证 2026-11-02 | 合成验证 | relative to the mean of the blocks above the absolute gate |
| `clip_evidence_max_marks` | 64 | 待标定 | 规格起始值 | upper bound on digital clip marks kept as evidence |

### `noise`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `frame_ms` | 100 | 合成验证 2026-11-02 | 合成验证 |  |
| `percentile` | 10 | 合成验证 2026-11-02 | 合成验证 | of A-weighted frame power |
| `snr_bad_below_db` | 35 | 待标定 | 规格起始值 |  |
| `snr_good_above_db` | 45 | 待标定 | 规格起始值 |  |
| `analysis_rate_max_hz` | 48000 | 合成验证 2026-11-02 | 合成验证 | higher rates are decimated by an integer factor first |
| `speech_frame_ms` | 50 | 合成验证 2026-11-02 | 合成验证 |  |
| `speech_top_percentile` | 95 | 合成验证 2026-11-02 | 合成验证 |  |
| `speech_within_top_db` | 15 | 合成验证 2026-11-02 | 合成验证 | active speech frames lie within this many dB of the top percentile |
| `speech_over_p10_db` | 10 | 合成验证 2026-11-02 | 合成验证 | and at least this far above the frame P10 |
| `min_frames` | 10 | 待标定 | 规格起始值 | fewer frames than this and the percentile is not trusted |

### `hum`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `welch_window_s` | 2 | 合成验证 2026-11-02 | 合成验证 |  |
| `welch_overlap` | 0.5 | 合成验证 2026-11-02 | 合成验证 |  |
| `analysis_rate_max_hz` | 48000 | 合成验证 2026-11-02 | 合成验证 |  |
| `local_median_halfwidth_hz` | 5 | 合成验证 2026-11-02 | 合成验证 |  |
| `peak_over_median_db` | 6 | 合成验证 2026-11-02 | 合成验证 |  |
| `stable_segment_db` | 3 | 待标定 | 规格起始值 |  |
| `stable_fraction` | 0.5 | 待标定 | 规格起始值 |  |
| `min_stable_harmonics` | 3 | 合成验证 2026-11-02 | 合成验证 |  |
| `count_harmonics_up_to` | 10 | 合成验证 2026-11-02 | 合成验证 |  |
| `harmonic_window_min_hz` | 1 | 合成验证 2026-11-02 | 合成验证 | search window = max(min_hz, frac * k) |
| `harmonic_window_frac` | 0.05 | 合成验证 2026-11-02 | 合成验证 |  |
| `f0_candidates_hz` | [50, 60] | 合成验证 2026-11-02 | 合成验证 |  |
| `f0_accept_hz` | 0.35 | 实录标定 2026-11-21 | 实录标定（2026-11）：四条观测的电流声基频偏差都不超过 0.01 Hz；还缺发电机、UPS 等漂移电源的录音 |  |
| `harmonic_max_hz` | 3000 | 合成验证 2026-11-02 | 合成验证 |  |
| `buzz_above_hz` | 1000 | 合成验证 2026-11-02 | 合成验证 |  |
| `whistle_min_hz` | 2000 | 合成验证 2026-11-02 | 合成验证 |  |
| `whistle_over_median_db` | 15 | 合成验证 2026-11-02 | 合成验证 |  |
| `whistle_max_hz` | 12000 | 实录标定 2026-11-21 | 实录标定（2026-11）：安静录音里的假窄峰（转换器、时钟音）在 17.3–23.0 kHz，都高于它；还没有真啸叫的录音，上限只被误报约束 | narrow peaks above this are not whistle candidates; real takes carry inaudible converter and clock tones up there |
| `whistle_min_dbfs` | -70 | 实录标定 2026-11-21 | 实录标定（2026-11）：所有假窄峰的单音电平最响 −79.7 dBFS，比它低 9.7 dB；还没有真啸叫的录音（例如 −50 dBFS 的窄音） | a narrow peak whose own power is below this is not a whistle, however far it stands above its neighbours |
| `slight_below_over_floor_db` | 16 | 实录标定 2026-11-21 | 实录标定（2026-11）：轻微的电流声谐波峰高出局部底噪 13.2 dB，明显的 19.5、24.0 dB，两侧各留约 3 dB；还缺第二批的电流声正例和真正响的电流声 | strongest harmonic over its local median below this: still reported, marked slight; the energy share is information only |
| `slight_below_dbfs` | -70 | 实录标定 2026-11-21 | 实录标定（2026-11）：观测到的四例电流声总电平在 −74.8 到 −88.0 dBFS，都比它低至少 4.8 dB、都判轻微；高于它的一侧还没有录音（缺一条响的电流声） | hum level (all detected harmonics) below this: still reported, marked slight; an absolute stand-in for 'relative to the voice', valid only once the gain step has brought the voice near its target |
| `f0_search_window_frac` | 0.3 | 合成验证 2026-11-02 | 合成验证 | f0 estimate: window around each nominal harmonic is max(harmonic_window_min_hz, frac * k) |
| `harmonic_energy_halfwidth_hz` | 1.5 | 合成验证 2026-11-02 | 合成验证 | energy share counts the excess over the local median within this distance of a harmonic |
| `whistle_exclude_halfwidth_hz` | 2 | 合成验证 2026-11-02 | 合成验证 | bins this close to a detected harmonic are not whistle candidates |
| `whistle_group_gap_hz` | 3 | 合成验证 2026-11-02 | 合成验证 | candidate bins further apart than this are separate whistle peaks |
| `level_floor_min_hz` | 20 | 合成验证 2026-11-02 | 合成验证 | lower edge of the band the energy share is measured against |
| `min_segments` | 3 | 待标定 | 规格起始值 | fewer Welch segments than this and the stability test is not trusted |

### `device_processing`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `zero_run_ms` | 50 | 合成验证 2026-11-02 | 合成验证 |  |
| `interior_only` | true | 合成验证 2026-11-02 | 合成验证 | leading and trailing zero runs do not count |
| `false_alarm_floor_16bit_dbfs` | -107 | 合成验证 2026-11-02 | 合成验证 | show nothing when the noise floor is below this |
| `evidence_max_marks` | 64 | 待标定 | 规格起始值 | upper bound on zero-run marks kept as evidence; the counts stay exact |

### `clap`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `analysis_rate_max_hz` | 48000 | 合成验证 2026-11-12 | 合成验证 | higher rates are decimated first; clipping and zero runs are still found at the original rate |
| `rel_db` | 25 | 合成验证 2026-11-12 | 合成验证 | a clap peaks within this many dB of the loudest one |
| `floor_dbfs` | -50 | 合成验证 2026-11-12 | 合成验证 |  |
| `min_gap_s` | 1 | 合成验证 2026-11-12 | 合成验证 | each picked clap masks half of this on either side; the earlier segment ends before the next clap |
| `onset_db` | 10 | 合成验证 2026-11-12 | 合成验证 | a clap peak stands this far above everything in the look-back window before it |
| `onset_lookback_ms` | 50 | 合成验证 2026-11-12 | 合成验证 |  |
| `onset_gap_ms` | 5 | 合成验证 2026-11-12 | 合成验证 | the look-back window stops this long before the candidate so its own rise is not compared with itself |
| `max_count` | 12 | 合成验证 2026-11-12 | 合成验证 |  |
| `segment_before_s` | 0.7 | 合成验证 2026-11-12 | 合成验证 |  |
| `noise_window_ms` | [-550, -100] | 合成验证 2026-11-12 | 合成验证 | relative to the clap peak |
| `peak_search_ms` | 5 | 合成验证 2026-11-12 | 合成验证 |  |
| `next_guard_ms` | 50 | 合成验证 2026-11-12 | 合成验证 |  |
| `clip_window_ms` | 10 | 合成验证 2026-11-12 | 合成验证 |  |
| `clip_min_run_samples` | 1 | 合成验证 2026-11-12 | 合成验证 | a clap that reaches the digital clip level for this many samples counts as clipped |
| `min_noise_ms` | 200 | 合成验证 2026-11-12 | 合成验证 | the noise window may be cut by the start of the capture, down to this length |
| `envelope_block_ms` | 10 | 合成验证 2026-11-12 | 合成验证 |  |
| `truncate_over_noise_db` | 4.77 | 合成验证 2026-11-12 | 合成验证 | the decay ends at the first envelope block below the noise plus this margin |
| `filter_order` | 4 | 合成验证 2026-11-12 | 合成验证 | Butterworth prototype order of the band filters |

### `reverb`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `bands_hz` | [500, 1000, 2000, 4000] | 待标定 | 规格起始值 |  |
| `fit_start_db` | -5 | 待标定 | 规格起始值 |  |
| `fit_end_db` | -25 | 待标定 | 规格起始值 |  |
| `band_pnr_min_db` | 35 | 待标定 | 规格起始值 | synthetic checks used 30; the final value comes from real recordings |
| `fit_r2_min` | 0.95 | 待标定 | 规格起始值 |  |
| `repeat_outlier_frac` | 0.3 | 待标定 | 规格起始值，待实录标定 | per band, claps further than this from the median are dropped |
| `repeat_min_kept` | 3 | 待标定 | 规格起始值，待实录标定 |  |
| `gate_margin_db` | 20 | 待标定 | 规格起始值，待实录标定 | a drop only counts as a gate while the band is still this far above its noise |
| `gate_tail_drop_db` | 10 | 待标定 | 规格起始值 | beyond the decay the fitted slope predicts |
| `gate_tail_drop_ms` | 5 | 待标定 | 规格起始值 |  |
| `gate_block_ms` | 1 | 合成验证 2026-11-12 | 合成验证 |  |
| `gate_before_ms` | 30 | 合成验证 2026-11-12 | 合成验证 |  |
| `gate_after_ms` | 30 | 合成验证 2026-11-12 | 合成验证 |  |
| `gate_search_from_ms` | 20 | 合成验证 2026-11-12 | 合成验证 | the window before a drop starts this long after the band peak, past the direct sound |
| `gate_zero_run_ms` | 1 | 合成验证 2026-11-12 | 合成验证 |  |
| `segment_after_s` | 1.6 | 合成验证 2026-11-12 | 合成验证 |  |
| `pnr_smooth_ms` | 5 | 合成验证 2026-11-12 | 合成验证 |  |
| `min_tail_ms` | 30 | 合成验证 2026-11-12 | 合成验证 |  |
| `dry_below_s` | 0.25 | 待标定 | 规格起始值 |  |
| `normal_below_s` | 0.45 | 待标定 | 规格起始值 |  |
| `boomy_below_s` | 0.7 | 待标定 | 规格起始值 |  |

### `direct_ratio`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `band_hz` | [1000, 4000] | 待标定 | 规格起始值 |  |
| `direct_window_start_ms` | -2.5 | 待标定 | 规格起始值 |  |
| `direct_window_end_ms` | 12 | 待标定 | 规格起始值 |  |
| `segment_after_s` | 1.2 | 合成验证 2026-11-12 | 合成验证 |  |
| `tail_snr_window_ms` | 50 | 合成验证 2026-11-12 | 合成验证 |  |
| `tail_snr_min_db` | 6 | 合成验证 2026-11-12 | 合成验证 | below this the tail is not measurable and only a lower bound is reported |
| `min_clean_claps` | 2 | 待标定 | 待实录标定 |  |

### `plosive`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `event_band_hz` | [20, 200] | 待标定 | 规格起始值 |  |
| `recording_band_hz` | [20, 120] | 待标定 | 规格起始值 | band to compare on real recordings |
| `low_f0_sensitivity_below_hz` | — | 待标定 | 待定：只有两个嗓音（约 120、240 Hz）且与话筒一一绑定；缺同一人用两支话筒，或第三个嗓音（150–180 Hz） | sensitivity drops at low fundamentals |
| `analysis_rate_max_hz` | 48000 | 待标定 | 规格起始值 |  |
| `filter_order` | 4 | 待标定 | 规格起始值 |  |
| `envelope_highpass_hz` | 80 | 待标定 | 规格起始值 |  |
| `envelope_frame_ms` | 10 | 待标定 | 规格起始值 |  |
| `envelope_hop_ms` | 0.5 | 合成验证 2026-11-23 | 合成验证：起点在一个周期内逐样本平移，读数的变化在容差内 | a rectangular frame's power ripples at twice the pitch; a coarse hop samples the ripple at a different place for every start point |
| `floor_percentile` | 10 | 待标定 | 规格起始值 | of the take's own envelope frames; the word list leaves pauses between words |
| `onset_rise_db` | 12 | 待标定 | 规格起始值 |  |
| `onset_over_floor_db` | 20 | 待标定 | 规格起始值 |  |
| `onset_baseline_ms` | [5, 30] | 待标定 | 规格起始值 | the rise is measured against the quietest frame this far before the frame |
| `onset_backtrack_db` | 6 | 待标定 | 规格起始值 |  |
| `onset_refractory_ms` | 80 | 待标定 | 规格起始值 |  |
| `syllable_merge_ms` | 130 | 待标定 | 规格起始值 | a burst followed by the vowel onset within this gap is one syllable |
| `quiet_before_ms` | 100 | 待标定 | 规格起始值 |  |
| `quiet_before_over_floor_db` | 20 | 待标定 | 规格起始值 |  |
| `event_end_over_floor_db` | 10 | 待标定 | 规格起始值 |  |
| `event_min_ms` | 20 | 待标定 | 规格起始值 |  |
| `peak_window_ms` | [-20, 150] | 待标定 | 规格起始值 |  |
| `vowel_band_hz` | [200, 4000] | 待标定 | 规格起始值 |  |
| `vowel_window_ms` | [100, 600] | 待标定 | 规格起始值 |  |
| `vowel_min_ms` | 40 | 待标定 | 规格起始值 |  |
| `vowel_within_db` | 6 | 待标定 | 规格起始值 |  |
| `vowel_kept_min_ms` | 15 | 待标定 | 规格起始值：原为 2.5 ms 帧距下的 6 帧，按毫秒写 | fewer kept steady-vowel frames: no reference; converted to envelope frames with the actual hop |
| `vowel_over_floor_db` | 15 | 待标定 | 工程起始值：依据实录，两位说话人在三个距离上，字表里真实音节的元音稳态都比底噪高 25 dB 以上；字表结束后碰到话筒架的低频闷响，后面只有底噪，约高 10 dB。取两者之间，与起音要高出底噪 20 dB 同一量级 | the steady vowel after an onset must be this far above the take's vowel-band floor; a low thump followed by room noise has no vowel and is not a syllable |
| `reference_floor_margin_db` | 3 | 待标定 | 规格起始值 |  |
| `evidence_max_marks` | 40 | 待标定 | 规格起始值 |  |
| `reference_max_db` | 9 | 待标定 | 经验值，依据实录标定：3 间房、2 位说话人、2 支话筒（2026-11）：两位说话人 5 cm 时每遍字表的最大值都超过它，60 cm 时都低于它；一位在 15 cm 处有一遍超过 | reference shown next to the worst syllable of each pass; above it the card gives a light hint, no grade |

### `sibilance`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `mid_high_band_hz` | [3000, 6000] | 待标定 | 规格起始值 |  |
| `high_band_hz` | [6000, 12000] | 待标定 | 规格起始值 |  |
| `reference_band_hz` | [200, 4000] | 待标定 | 规格起始值 |  |
| `min_effective_bandwidth_hz` | 10000 | 待标定 | 规格起始值 | below this the high band is switched off |
| `moderate_above_db` | — | 待标定 | 待实录标定 |  |
| `heavy_above_db` | — | 待标定 | 待实录标定 |  |
| `analysis_rate_max_hz` | 48000 | 待标定 | 规格起始值 |  |
| `frame_ms` | 10 | 待标定 | 规格起始值 |  |
| `hop_ms` | 1.25 | 合成验证 2026-11-23 | 合成验证：起点在一个周期内逐样本平移，读数的变化在容差内 | one spectrum frame this often; frames overlap |
| `wideband_hz` | [80, 20000] | 待标定 | 规格起始值 |  |
| `noise_frame_percentile` | 20 | 待标定 | 规格起始值 | frames at or below this percentile of the wideband level make the noise spectrum |
| `centroid_band_hz` | [1000, 16000] | 待标定 | 规格起始值 |  |
| `centroid_min_hz` | 3000 | 待标定 | 规格起始值 |  |
| `flatness_min` | 0.1 | 待标定 | 规格起始值 | geometric over arithmetic mean of the 3-12 kHz bins |
| `unvoiced_band_hz` | [200, 1500] | 待标定 | 规格起始值 |  |
| `unvoiced_over_db` | 3 | 待标定 | 规格起始值 |  |
| `over_noise_db` | 10 | 待标定 | 规格起始值 |  |
| `event_min_ms` | 40 | 待标定 | 规格起始值 |  |
| `gap_max_ms` | 15 | 待标定 | 规格起始值 |  |
| `noise_subtract_floor_db` | -10 | 待标定 | 规格起始值 | after subtracting the noise spectrum each bin keeps at least this much of the noise |
| `bandwidth_over_noise_db` | 10 | 待标定 | 规格起始值 | effective bandwidth: highest point whose band the speech stays this far over the band noise floor; the search stops after bandwidth_stop_points failing points in a row |
| `bandwidth_search_from_hz` | 1000 | 待标定 | 规格起始值 |  |
| `bandwidth_band_hz` | 1000 | 待标定 | 规格：实录里说话几乎不停的一段被按最安静帧的底噪误判成 2.1 kHz（实际高于 16 kHz），改用频带底噪、按 500 Hz 频点走、连续 2 点不满足才停 | each point looks at a band this wide around it |
| `bandwidth_step_hz` | 500 | 待标定 | 规格：实录里说话几乎不停的一段被按最安静帧的底噪误判成 2.1 kHz（实际高于 16 kHz），改用频带底噪、按 500 Hz 频点走、连续 2 点不满足才停 | distance between the points |
| `bandwidth_stop_points` | 2 | 待标定 | 规格：实录里说话几乎不停的一段被按最安静帧的底噪误判成 2.1 kHz（实际高于 16 kHz），改用频带底噪、按 500 Hz 频点走、连续 2 点不满足才停 | stop after this many failing points in a row |
| `bandwidth_noise_percentile` | 2 | 待标定 | 规格：实录里说话几乎不停的一段被按最安静帧的底噪误判成 2.1 kHz（实际高于 16 kHz），改用频带底噪、按 500 Hz 频点走、连续 2 点不满足才停 | the noise floor of a band is this percentile of its per-frame power over all frames |
| `bandwidth_active_percentile` | 95 | 待标定 | 规格起始值 |  |
| `bandwidth_active_within_db` | 15 | 待标定 | 规格起始值 |  |
| `evidence_max_marks` | 40 | 待标定 | 规格起始值 |  |
| `vowel_gap_ms` | 30 | 待标定 | 规格起始值：与实录分析同一种取法 | the vowel reference starts this long after the fricative ends |
| `vowel_window_ms` | 300 | 待标定 | 规格起始值：与实录分析同一种取法 | and ends this long after the fricative ends |
| `vowel_centroid_max_hz` | 3500 | 待标定 | 规格起始值：与实录分析同一种取法 | vowel frames have their centroid below this |
| `vowel_within_db` | 12 | 待标定 | 规格起始值：与实录分析同一种取法 | and are within this of the loudest frame in the window |
| `vowel_kept_min_ms` | 20 | 待标定 | 规格起始值：与实录分析同一种取法，原为 5 ms 帧距下的 4 帧，按毫秒写 | fewer kept frames: this fricative has no vowel reference; converted to spectrum frames with the actual hop |
| `vowel_loudest_frac` | 0.5 | 待标定 | 规格起始值：与实录分析同一种取法 | the vowel level is the mean of this share of the loudest kept frames |
| `reference_max_db` | -3 | 待标定 | 经验值（常见做法的默认），依据实录标定：3 间房、2 位说话人、2 支话筒（2026-11）：两位说话人在唱歌位置（约 15 cm）6–12 kHz 对元音的中位比值分别为 −6.8、−8.4 dB，都低于它；一位挪到 60 cm 后升到 −4.1 dB，比值随距离的方向不稳，只作参考 | reference for the 6-12 kHz over vowel ratio; not shown when the high band is off |

### `click`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `highpass_hz` | 2000 | 待标定 | 规格起始值 |  |
| `fricative_min_ms` | 30 | 待标定 | 规格起始值 |  |
| `analysis_window_s` | 15 | 待标定 | 规格起始值 | length of natural speech the session captures for this analysis; the analysis itself takes whatever it is given |
| `residual_median_multiple` | 20 | 待标定 | 规格起始值，待实录标定 | LPC residual over its local scale; starting range 15-20 adjusted on real recordings, uncalibrated |
| `moderate_per_min` | — | 待标定 | 待定：口水音只作提示、不分档；缺第二位说话人的试听标签和安静段每分钟的误报率 |  |
| `heavy_per_min` | — | 待标定 | 待定：口水音只作提示、不分档；缺第二位说话人的试听标签和安静段每分钟的误报率 |  |
| `analysis_rate_max_hz` | 48000 | 待标定 | 规格起始值 |  |
| `filter_order` | 4 | 待标定 | 规格起始值 |  |
| `candidate_multiple` | 10 | 待标定 | 规格起始值 | residual samples over this multiple are grouped into candidates; the candidate's own peak must reach residual_median_multiple |
| `lpc_order` | 32 | 待标定 | 规格起始值 |  |
| `lpc_window_ms` | 25 | 待标定 | 规格起始值 |  |
| `lpc_block_ms` | 2 | 合成验证 2026-11-22 | 合成验证：起点在一个周期内逐样本平移，读数的变化在容差内 | a predictor is estimated at the centre of every block; each sample interpolates the coefficients of the two nearest centres |
| `lpc_white_noise_frac` | 0.0001 | 待标定 | 规格起始值 | added to the zero-lag autocorrelation so the predictor stays stable |
| `scale_block_ms` | 10 | 待标定 | 规格起始值 |  |
| `scale_hop_ms` | 1 | 合成验证 2026-11-22 | 合成验证：起点在一个周期内逐样本平移，读数的变化在容差内 | one block median this often; each sample interpolates the scale of the two nearest block centres |
| `scale_trim_frac` | 0.45 | 合成验证 2026-11-22 | 合成验证：起点在一个周期内逐样本平移，读数的变化在容差内；合成元音起止处的基音脉冲仍在计数门限以下 | the local scale is the mean of the block medians within the half width after dropping this share at each end; a plain median jumps between the speech and the pause blocks, a wider kept band lowers the scale near speech edges and lets pitch pulses through |
| `scale_halfwidth_ms` | 150 | 待标定 | 规格起始值 | running median of the block medians of the residual magnitude |
| `spike_merge_ms` | 5 | 待标定 | 规格起始值 |  |
| `peak_window_ms` | [-2, 4] | 待标定 | 规格起始值 |  |
| `envelope_ms` | 2 | 待标定 | 规格起始值 |  |
| `fricative_start_ms` | 4 | 待标定 | 规格起始值 |  |
| `fricative_median_db` | -10 | 待标定 | 规格起始值 | high-passed envelope after the spike, relative to the spike's envelope peak |
| `fricative_min_db` | -15 | 待标定 | 规格起始值 |  |
| `plosive_low_band_hz` | [20, 300] | 待标定 | 规格起始值 |  |
| `voice_band_hz` | [200, 4000] | 待标定 | 规格起始值 |  |
| `plosive_before_ms` | [-30, -5] | 待标定 | 规格起始值 |  |
| `plosive_after_ms` | [10, 60] | 待标定 | 规格起始值 |  |
| `plosive_rise_db` | 10 | 待标定 | 规格起始值 |  |
| `speech_frame_ms` | 50 | 待标定 | 规格起始值 |  |
| `speech_hop_ms` | 1 | 合成验证 2026-11-22 | 合成验证：起点在一个周期内逐样本平移，读数的变化在容差内 | speech level frames overlap; one frame this often, each frame a whole number of these |
| `speech_percentile` | 95 | 待标定 | 规格起始值 |  |
| `speech_within_db` | 15 | 待标定 | 规格起始值 |  |
| `speech_floor_percentile` | 10 | 待标定 | 规格起始值 |  |
| `speech_over_floor_db` | 10 | 待标定 | 规格起始值 | the loud frames must stand this far above the quiet ones, otherwise there is no speech to measure against |
| `min_rel_speech_db` | -35 | 待标定 | 规格起始值 |  |
| `context_search_ms` | 5 | 待标定 | 规格起始值 |  |
| `context_window_ms` | [4, 40] | 待标定 | 规格起始值 |  |
| `context_max_rel_db` | -14 | 待标定 | 规格起始值，待实录标定 | high-frequency background around the spike, relative to the speech; louder backgrounds are sibilant onsets or aspiration; adjusted on real recordings, uncalibrated |
| `cluster_gap_ms` | 100 | 待标定 | 规格起始值 |  |
| `evidence_max_marks` | 40 | 待标定 | 规格起始值 |  |
| `reference_max_per_min` | 75 | 待标定 | 经验值，依据实录（3 间房、2 位说话人、2 支话筒，2026-11）：两位说话人在约 15 cm 处的自然读数都低于它，一位刻意咂嘴时高于它，另一位刻意咂嘴时与正常读数相近；检出里仍有送气音（t、k）的误报，只作参考 | reference for clicks per minute; above it the card gives a light hint, no grade |

### `direction`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `repeat_trigger_db` | 6 | 待标定 | 规格起始值 |  |
| `priority_db` | 10 | 待标定 | 规格起始值 |  |
| `small_difference_db` | 3 | 待标定 | 规格起始值 |  |
| `brightness_num_band_hz` | [3000, 10000] | 待标定 | 规格起始值 |  |
| `brightness_den_band_hz` | [300, 1000] | 待标定 | 规格起始值 |  |
| `frame_ms` | 100 | 合成验证 2026-11-12 | 合成验证 |  |
| `hop_ms` | 5 | 合成验证 2026-11-23 | 合成验证：起点在一个周期内逐样本平移，读数的变化在容差内 | frames overlap; one frame this often, each frame a whole number of these |
| `active_top_percentile` | 95 | 合成验证 2026-11-12 | 合成验证 |  |
| `active_within_top_db` | 20 | 合成验证 2026-11-12 | 合成验证 | active frames lie within this many dB of the top percentile |

### `distance`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `dr_resolution_db` | 3 | 待标定 | 工程起始值，依据实录：同一位置各下拍手的 D/R 差 0.3–2 dB，15 cm 挪到 60 cm 时降 4.3–5.2 dB（两间房） | a retest counts as a sign only when the D/R moved by more than this; a lower bound only rules out one direction |
| `plosive_resolution_db` | 6 | 待标定 | 工程起始值，依据实录：60 cm 处字表最大值两人分别 7.3、7.7 dB，15 cm 处出现过单个 17.3 dB 的音节；缺同一位置重复字表的散布 | a retest counts as a sign only when the worst syllable moved by more than this |
| `sibilance_resolution_db` | 5.2 | 待标定 | 工程起始值，依据实录：同一增益下两次测试的擦音中位电平，95% 置信能分辨的差两人分别约 5.2、1.6 dB，取较大的 | 6-12 kHz level after noise subtraction, same gain only; a retest counts as a sign only when it moved by more than this |
| `critical_distance_coef` | 0.057 | 待标定 | 规格起始值：临界距离公式（扩散声场近似） | critical distance in metres = coef x sqrt(directivity x volume / T20) |
| `room_volume_m3` | 30 | 待标定 | 工程起始值：典型卧室，约 3 × 4 × 2.5 m | typical room volume for the empirical distance limit; the plugin cannot measure the volume |
| `source_directivity` | 2 | 待标定 | 工程起始值：人声正前方的指向性因子；话筒自身的指向性不计入，上限因此偏近 |  |
| `limit_critical_frac` | 0.5 | 待标定 | 工程起始值：临界距离的一半，扩散声场下那里的直达声比房间声高约 6 dB | the empirical distance limit is this fraction of the critical distance |

### `recommended_distance`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `condenser_around_cm` | 15 | 待标定 | 产品约定：常见做法，维护者确认（2026-11） | condenser, USB and not sure: about this far |
| `condenser_range_cm` | [10, 30] | 待标定 | 产品约定：常见做法，维护者确认（2026-11） | condenser, USB and not sure: stay within |
| `dynamic_range_cm` | [5, 10] | 待标定 | 产品约定：常见做法，维护者确认（2026-11） | dynamic: about this far; headset: none |

### `priority`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `direction_impact` | 9 | 待标定 | 工程起始值 | impact of fixing: turning the microphone the right way round costs almost nothing and fixes the most; score = impact x confidence / cost |
| `direction_cost` | 1 | 待标定 | 工程起始值 | relative effort of the fix: turning the microphone the right way round costs almost nothing and fixes the most |
| `room_impact` | 9 | 待标定 | 工程起始值 | impact of fixing: room advice (move closer, treat or change the room); score = impact x confidence / cost |
| `room_cost` | 1.2 | 待标定 | 工程起始值 | relative effort of the fix: room advice (move closer, treat or change the room) |
| `hum_impact` | 6 | 待标定 | 工程起始值 | impact of fixing: electrical hum, buzz or whistle (slight hum is never a candidate); score = impact x confidence / cost |
| `hum_cost` | 1 | 待标定 | 工程起始值 | relative effort of the fix: electrical hum, buzz or whistle (slight hum is never a candidate) |
| `gain_impact` | 5 | 待标定 | 工程起始值 | impact of fixing: input gain outside the good band (digital clipping is fixed first and not scored here); score = impact x confidence / cost |
| `gain_cost` | 1 | 待标定 | 工程起始值 | relative effort of the fix: input gain outside the good band (digital clipping is fixed first and not scored here) |
| `noise_impact` | 6 | 待标定 | 工程起始值 | impact of fixing: broadband noise with a bad A-weighted SNR; score = impact x confidence / cost |
| `noise_cost` | 1.5 | 待标定 | 工程起始值 | relative effort of the fix: broadband noise with a bad A-weighted SNR |

### `session`

| 名称 | 值 | 状态 | 依据 | 说明 |
|---|---|---|---|---|
| `gain_s` | 8 | 待标定 | 规格起始值 |  |
| `quiet_s` | 8 | 待标定 | 规格起始值 |  |
| `room_max_s` | 20 | 待标定 | 规格起始值 |  |
| `room_claps_needed` | 4 | 待标定 | 规格起始值 |  |
| `room_claps_max` | 8 | 待标定 | 规格起始值 |  |
| `live_clap_every_s` | 0.5 | 待标定 | 工程起始值 | how often the room step re-reads the claps so far to give per-clap feedback; with manual phases the soft claps of the position step are re-read as often, to count the clean ones |
| `soft_claps` | 3 | 待标定 | 规格起始值 |  |
| `soft_clap_round_s` | 2 | 待标定 | 规格起始值 |  |
| `word_list_rounds` | 2 | 待标定 | 规格起始值 |  |
| `word_list_round_s` | 10 | 待标定 | 规格起始值 |  |
| `direction_take_s` | 3 | 待标定 | 规格起始值 |  |
| `notice_s` | 3 | 待标定 | 工程起始值 | with automatic phases, countdown shown before a phase or step restarts by itself; counted in samples. With manual phases the phase waits for the user to start it again |
| `restarts_max` | 3 | 待标定 | 工程起始值 | how many times one step may restart a phase by itself (clipped soft claps, gated claps); after that it carries on and the analysis reports the retest |
| `realtime_ratio_max` | 1.5 | 待标定 | 工程起始值 | audio arriving faster than this multiple of real time means an offline render the host did not announce; the step is voided |
| `realtime_check_min_s` | 1 | 待标定 | 工程起始值 | wall-clock time before the mid-step speed check starts; the check at the end of a step always runs |
| `capture_buffer_s` | 4 | 待标定 | 工程起始值 | audio the capture ring holds while the worker is busy; a full ring voids the step as an overflow |
| `evidence_hop_min_s` | 0.002 | 待标定 | 工程起始值 | narrowest cell of a step's evidence envelope, rounded up to whole samples |
| `evidence_cells_max` | 8192 | 待标定 | 工程起始值 | most cells in one step's evidence envelope; longer steps merge whole narrow cells into wider ones |
| `evidence_events_max` | 200 | 待标定 | 工程起始值 | most plosive, sibilance and mouth-noise events in one step's evidence; the earliest are kept |
| `gain_min_voice_s` | 3 | 待标定 | 工程起始值 | with manual phases, voiced seconds the gain take needs before the user may end it; voiced frames follow the speech level rule of the noise group |
| `word_list_min_events` | 6 | 待标定 | 工程起始值 | with manual phases, syllables the plosive or the sibilant list needs before the user may end it, counted by that list's own analysis; the shortest list has six words |
| `natural_min_voice_s` | 8 | 待标定 | 工程起始值 | with manual phases, voiced seconds of natural reading before the user may end it; mouth noise is counted over the word lists and this reading. The reading is capped at click.analysis_window_s (15 s) and only about 70 % of natural reading counts as voiced: on six natural-reading recordings every 15 s stretch reached 8 s but only 82 % reached 10 s. To be calibrated on more recordings |
| `direction_min_voice_s` | 1.5 | 待标定 | 工程起始值 | with manual phases, voiced seconds each direction take needs before the user may end it |
| `click_guard_s` | 0.3 | 待标定 | 工程起始值 | with manual phases, audio dropped after the user starts a phase and before the user ends it, so the mouse click is not analysed |
| `live_words_every_s` | 1 | 待标定 | 工程起始值 | with manual phases, how often a word list is re-analysed to count its syllables, until there are enough |
<!-- thresholds:end -->
<!-- prettier-ignore-end -->
