# R33-T05 BASELINE — AgentOps 与 Mission 持久化、恢复及交付

执行时间：2026-11-18（UTC）
研究快照：2026-11-17 master `5ff15400047a6c55892669c9e9911e0e3b51bd04`

## 实际执行基线（运行时刷新）

- **BASE_SHA**: `1dfb04382e0fda43ee99665bed8b98666b3f92d7`（origin/master，默认分支 master，已核实 via gh api）
- 快照 → 本轨 base 之间新增 5 个合并提交：
  - `a060fa3d2c` #2134 fix(agent): Pi lifecycle + Windows UTF-8 SDK paths（**已合并 2026-11-17T14:16:48Z**，最终 head `f7b0ebb9c6ce37ace97388d89f585f9d82f036eb`，比研究快照的 a6fbc9fc 新）
  - `f64cd939fa` #2135、`07c593dbd0` #2136、`56f0bd2a0a` #2137（均已合并）
  - `1dfb04382e` #2139 fix: Unicode profile paths + bilingual startup
- **开放 PR：0**（快照中的 #2134–#2137 全部已合并；核实时间 2026-11-18）
- 远端分支 20 条：master + 10 codex/* + 9 zcode/*（旧轨留存；无 T05 在途新分支）
- 开放 issues：106（详见 ISSUE_TRIAGE.md）

## 与快照的差异（重要）

1. 快照的全部 "开放 PR 保留区" 已随 #2134–#2137 合并进入 master。本轨通过最新 master 继承，不再回避这些路径；但避免重复实现同一修复。
2. `resources/translations/sicnu_zh_CN.ts` 不再被开放 PR 占用 → 按公共规则 TS 统合归 T06；本轨只提供精确 context/source/translation HANDOFF。
3. 5 项静态候选在 BASE_SHA 上逐条核验（见 LEDGER.md 第 1 轮）：全部 SOURCE_CONFIRMED（代码仍在），动态复现/修复状态逐项推进。

## 本轨 worktree

- 分支：`zcode/r33-t05-agentops-mission-20261117T173601Z`
- 路径：`/home/kevin/projects/rs-studio/exp-rs-r33-t05-agentops-mission-20261117T173601Z`
- 构建：`build-dev`（preset `dev-default`，Debug，单配置）
- 主机锁：`$HOME/.cache/exp-rs-agent/host-build`（与其他轨共用）；本轨 build 锁：`build-dev`
