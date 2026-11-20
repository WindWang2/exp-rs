# R35-T01 ISSUE_TRIAGE（针对本切片涉及的 issue 子项）

执行时点：2026-11-19；open issues 106（含 PR），直接 issue 覆盖状态沿任务快照。

| Issue | 子项 | 本切片处置 |
|---|---|---|
| #2092 | Landsat L2 band identity/affine | WP01 实现修复+新回归（SR_Bn 关联 MTL MULT/ADD）。PR 用 Refs #2092（完整关闭需 E2E-01 全链与更多产品变体验证） |
| #2093 子项 | PCA helper 参数域 | WP02 实现修复+回归。Refs #2093 |
| #2096 | MAD 无信息状态 | WP04 实现修复+回归（含 IR-MAD 同病）。Refs #2096 |
| #2097 | VCA 有效谱集合 | WP05 实现修复+回归。Refs #2097 |
| #2101 | target/interference 来源独立 | 未开工（切片2候选） |
| #2103 子项 | apply_mask dtype、quality_mosaic、terrain_flow、viewshed、temporal monitor | 未开工（切片2候选） |
| #2105 | measured/synthetic/license/citation | 未开工 |

其余 20 个 WP 均未占用既有 open PR；本切片无与其他轨重叠的写集。
