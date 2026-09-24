# PR body (draft) — hardening/r3-repair-planner-recovery-integration-r3

## Goal

R3 Track 05: 把 #1278 的 repair planner、#1286 的 agent_ops recovery 从"并存"变成"真接线"——live CapabilityKnowledge 生成候选、preflight findings 驱动 requirement synthesis、审批用绑定 token、修复后强制重验证。不新增产品方向，不做 repair scheduler，planner 保持 planning-only。

基线 master：`9ea5a2fd17317d924ac2c234286650b48c0b02bf`。

## 去重

Open PR 仅 #1314（model runtime operators）、#1312（app workbench shell）——与本模块无交集（#1312 同触 `tests/CMakeLists.txt`，本 PR 未改该文件，无冲突面）。无 open issues。远端 `hardening/r3-agent-ops-live-driver-r3` 等分支指向已合入的 master 历史，已吸收，未 rebase。

## 根因 / Oracle

1. **伪造计划**（recovery_bridge.cpp 基线 8-43）：投影绕过 `planRepairsForFindings`，对每个 proposal 手写 `RepairAction`，伪造 `shape_preserving/low/light`，`resolvesAllBlockers = !proposals.empty()`。
   Oracle：`recovery repair projection is provider-backed planning with real facts` 等 5 个新 case，RED 时 ctest 输出 `"repair"=="ask"`、`"no_safe_repair"=="planned"`。
2. **caller claim 压过真实风险**：science gate 只读 `ctx.leadingRiskClass`。
   Oracle：`the plan's own risk class drives the science-changing gate, not the caller claim`——RED 时 radiometric 修复在 claim=shape_preserving 下直接 auto。
3. **裸 bool 审批**：`setPendingRepairApproval(true)` 即武装，无绑定/过期/replay。
   Oracle：token 矩阵 + coordinator arm/consume/replay + surface 三个 case。
4. **live adapter 缺失**：`buildFromCapabilityEntries`/`planRepairsForFindings` 基线零生产调用方。
   Oracle：`live repair capability source routes the real knowledge documents`（真文档 + 删除 rs:resample 后不得复活）。
5. **planning state**：负 sequence 被接受（RED），重启后 eviction/审计连续性与 digest link 负例补强（回归守卫）。

全部 RED 证据见 `docs/.../02-test-ledger.md`（含 expansion 输出摘要与复现命令）。

## Authority / 单一真相

- 候选唯一来源：`CapabilityKnowledge` →（只读 adapter）→ provider seam；bridge 不携带 operator 表。
- 风险唯一来源：planner 的 closed per-kind 候选合同；审批唯一入口：绑定 token；autonomy 推导复用 `gateMutatingOp` 本身。
- quality_mask：上游无 finding 生产者（全仓 grep 证据），按 Track 约束不发明。

## Changed files

- 新增 `src/agent_ops/repair_approval.{h,cpp}`、`src/agent/harness/repair_capability_source.{h,cpp}`
- 修改 `src/agent_ops/recovery_bridge.{h,cpp}`、`operations_coordinator.{h,cpp}`、`ops_types.{h,cpp}`、`session_surface.cpp`、`src/repair_planner/repair_state.cpp`
- CMake：`src/agent_ops/CMakeLists.txt`（+1 源）、`src/agent/CMakeLists.txt`（+2 源、链接 Qt-free 叶子 `sicnu_repair_planner`）
- 测试：`tests/test_agent_ops_core.cpp`、`tests/test_repair_planner_completion.cpp`、`tests/test_capability_knowledge.cpp`
- 文档：`docs/development/hardening/repair-planner-recovery-integration-r3/`

## 定向构建 / 测试（未做全量编译）

`narrow_targets.py --base origin/master --json` 映射的最小闭包：
`test_agent_ops_core`、`test_repair_planner_completion`、`test_capability_knowledge`、`test_build_wiring_drift`、（邻接）`test_repair_planner_schema`；库目标 `sicnu_agent_ops`、`sicnu_agent`、`sicnu_repair_planner`。构建 `-j2`（压力降 `-j1` 备用）；ctest 全部带精确 `-R`。关键 oracle 双跑确认。

## Review findings / fixes

见 `05-adversarial-review.md`（P0/P1/P2 修复后 re-review）。

## Known limits

- token digest 是 tamper-evident（与 planning state/agent_plan 同纪律），不是 MAC；跨进程持久化的消费记录留待执行记录域。
- `run()` 的 post-hoc recovery 仍是 advisory ask（loop 状态机权威不变）；审批绑定的是 `evaluateRecovery()` 投影的计划（驱动标准路径），run 消费 armed token。
- Windows 专项：无平台相关新代码（jsoncpp/sha256/原子计数均跨平台）。

## Rollback

单分支 revert 即可恢复裸-bool 审批语义与伪造投影；无 schema/数据迁移（token 为内存态 wire 文档）。
