# R22-01 science FINAL_REPORT

## 交付概要
分支 `agent/r22-science-20261025T133250Z`（master@911e0b17 + 5 commits），
修复 4 个 science Issue（3×P1 + 1×P2），新增 3 个测试二进制（13 用例，
552,601 断言），双轮真正独立的对抗评审闭环。

| Issue | 严重度 | 修复 | Commit |
|---|---|---|---|
| #1707 distanceToForeground 原地覆盖 f | P1 | 下包络求值写入独立输出缓冲（F-H 算法本征要求） | 3beab0a |
| #1684 累加器方差 float 均值失真 | P1 | double 均值 + OBIA Kahan 累加 + 契约文档化 | eeb1711 |
| #1685 seed=42 跨 STL 不可复现 | P1 | portable_random.h 固定算法原语 + 9 调用点替换 + 黄金锚点 | e79455d |
| #1686 重采样半像元边界丢失 | P2 | 足迹改为栅格边缘 [-0.5, w-0.5]，边缘钳制采样 | b113e41 |
| R1 评审闭环（1 P2 + 8 P3） | - | 头契约/注释/守卫/黄金 pin 等 9 项 | eb41870 |

## 验证结构（详见 TEST_RESULTS.md / RED_GREEN.md / REVIEW_R1.md / REVIEW_R2.md）
- RED：四个 Issue 的独立复现全部先失败（含 issue 原始反例、stddev 64≠0.25 等）。
- GREEN×2：13 个测试二进制两轮全绿；351,467 断言随机 mask 与暴力 O(N²) 对拍零错误。
- R1：独立评审 APPROVE；评审员自行重算全部黄金值（from-scratch MT19937）、
  2 万 mask 对拍、复现修复前失败数值、补建并运行 11 个风险测试二进制。
- R2：第二独立评审（强制反例构造 + R1 修复引入新错的专项复核）。

## 排重与协调
- #1676（R21-A open PR）源码级比对：未修 #1684（同函数仍 float 均值）、未修
  #1685 kmeans 点；本轨改动与其在 math_utils.cpp（不同函数）、rs_kmeans_operator.cpp
  （不同区域）有轻度文本交集，git 可自动合并，语义无冲突。
- 其余 open PR（#1673/74/75/77）与本轨改动文件零交集。

## 遗留与移交（详见 CROSS_TRACK_FINDINGS.md）
- #1706 FeatherAccumulator（P1，在允许范围）：容量所限未实施，移交 science 下一里程碑。
- #1687（P3）：-999 魔数改波段声明 NoData 需动签名与全调用方，建议随 #1676 合并考虑；
  validCount 语义已文档化。
- src/app 分类窗口朴素累加（#1684 同类残余风险，P3）：本轨无该目录写权限，已记录建议。
- Windows/macOS 实机未跑（单主机）；跨平台主张基于标准条款 + 黄金锚点（任何偏离
  在任一 STL 立即 RED），非多 STL 实测。

## 不执行项声明
- 未跑全仓 ctest（按最窄→受影响族→集成策略，R1 又补 11 个二进制）。
- 线上 CI 不参与完成门；PR 不等待 CI。
- 无性能主张（PERF_BASELINE.md = N/A + 结构性开销说明）。
