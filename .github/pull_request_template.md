<!-- Derived from synchain-oss/rinne; see upstream/ci.json -->
<!--
PR 标题与 commit 同格式：type(scope): 中文描述，不写任务编号或任何内部编号。
不适用的栏目写「不涉及」，不要删掉标题。正文里不写 AI 署名（Generated with … 一类）。
最后一行必须是与 commit 作者一致的 Signed-off-by（branch-gate 会核对），把占位的姓名与邮箱换成你自己的。
-->

## 做了什么


## 来源 SHA
<!-- 复制自参考仓的代码：upstream/<area>.json 里登记的 sha 与范围；没有复制就写「不涉及」 -->


## 契约自查
<!-- 改了 Result 契约、阈值（thresholds.json）、桥 API、state 格式之一：同 PR 更新 docs/contracts/ 并说明；
     新增 docs/contract-changes/<YYYYMMDD>-<slug>.md，或单独写一行「contract-neutral: 理由」（branch-gate 会核对） -->
- [ ] Result 契约
- [ ] 阈值（thresholds.json，代码里没有魔法数）
- [ ] 桥 API
- [ ] state 格式

## 实时 / 直通 / 隐私自查
- [ ] processBlock 无分配 / 锁 / I/O / 日志 / 异常 / 墙钟
- [ ] 音频逐位直通，没有为测试静音或改动输出；NaN / Inf 输入不会让分析状态失效
- [ ] 音频只在内存里：没有写盘、上传、录音文件，也没有把音频放进日志或 issue
- [ ] 仓内只有合成夹具（每个 .wav ≤ 200 KB，合计 < 5 MB）

## 验证
本地 gates：<档> @<sha7>：PASS n / FAIL 0 / SKIP n / FLAKY n（EXIT=0）
<!-- 另列新增或修改的测试；需要分支 dispatch 时附 run 链接 -->


## 宿主实测（可选）
| 宿主 | 实时 | 离线导出 | 备注 |
|---|---|---|---|
|  |  |  |  |

## 界面截图（UI 改动）
<!-- zh / en 并排 -->


## DCO
- [ ] 所有 commit 均用 `git commit -s` 签名，且无 AI 署名

Signed-off-by: 姓名 <邮箱>
