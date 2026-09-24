# 00 — Recon Baseline: repair-planner-recovery-integration-r3

Track: R3 Track 05 — Repair Planner / Live Capability / Agent Recovery Integration。
基线: origin/master = `9ea5a2fd17317d924ac2c234286650b48c0b02bf`（recon seed `3487b9ad8` 已被 #1295–#1315 前移；recon 时无 open issues）。
Worktree: `../exp-rs-r3-repair-planner-recovery-integration-r3`，分支 `hardening/r3-repair-planner-recovery-integration-r3`。

## Open PR 去重（recon 时点）

- **#1314** `hardening/r3-plugin-model-crash-provenance-r3` — 只触及 `src/operators/runtime/model_*`、tile 推理与对应测试。与 repair_planner / agent_ops / capability 无交集。
- **#1312** `hardening/r3-workbench-full-shell-recovery-r3` — app/workbench shell（`src/app/`、main_window*、project_context）。触及 `tests/CMakeLists.txt`（本 PR 也触及测试，但仅对既有 target 内部追加 TEST_CASE，未新增 test target、未改中央 CMake 除 `src/agent_ops/CMakeLists.txt` 与 `src/agent/CMakeLists.txt` 两处模块内 delta）。无逻辑冲突。
- 结论：无同模块并行 PR。

## 历史线索分类

- `hardening/r3-agent-ops-live-driver-r3`（远端分支）— 指向 `5697ca2ad`（已在 master 历史内），无未合内容；**已吸收**。
- `rs14-unified-verifier`、`agent/flash-*`、旧 `feat/*` — 均落后 master 且与本模块无未覆盖缺口；**纯历史/已吸收**，未 rebase / cherry-pick。
- #1278（repair planner completion）、#1286（agent_ops production session）、#1279（preflight）、#1285（verifier）已全部在基线内 —— 本 PR 是其后的**接线与收敛**层。

## 尾项确认（执行时重验，file:line 基于 9ea5a2fd）

1. `JsonRepairCapabilityProvider::buildFromCapabilityEntries` 与 `planRepairsForFindings` **无任何生产调用方**（仅 tests 引用）——live CapabilityKnowledge adapter 缺失。
2. `RecoveryBridge::projectRepairPlan`（recovery_bridge.cpp:8-43 基线版）**绕过真实 planner**：对每个 diagnostic.proposal 手工构造 `RepairAction`，伪造 `riskClass=shape_preserving`、`severity=low`、`cost.rank=2/costClass=light`，并把 `resolvesAllBlockers` 设为 `!proposals.empty()` —— 计划文档在无任何执行证据时宣称 blocker 可解。
3. `decide()` 的 science-changing gate 只读 `ctx.leadingRiskClass`（调用方 claim），计划文档自身却全部标 shape_preserving —— claim 与文档互相矛盾。
4. 批准 seam 是**裸 bool**：`setPendingRepairApproval(bool)` / `OpsRunRequest::approvePendingRepair`（operations_coordinator.h:82-83,37），无 plan 绑定、无过期、无 coordinator 绑定、无 replay 防护；surface `approve_repair` 接受任意 `{"approve":true}`。
5. `RepairPlanningState::validRecord` 接受负 sequence（repair_state.cpp:82-88）——坏时钟输入静默进入 eviction 队头。
6. quality_mask finding：`findingCodeTable` 无任何 code 映射到 `kQualityMask`；preflight/verifier/harness_error 均无 mask 类 finding 生产者。**按目标 5 不凭空发明**，仅在计划文档的 radiometric 替代族中保持既有合法用法（#1278 已测）。
