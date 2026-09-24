# 03 — Implementation Ledger: repair-planner-recovery-integration-r3

## 变更面（文件 → 意图）

**新增**
- `src/agent_ops/repair_approval.{h,cpp}` — 审批 token：绑定 (plan_id, coordinator 实例 id, [issued,expires]) + sha256/16 完整性 digest；`mintRepairApprovalToken`（参数不可用→null，绝不铸造裸批准）、`verifyRepairApprovalToken`（malformed/tampered/wrong_plan/wrong_coordinator/expired 五类 typed 拒绝；无可用时钟 = 过期，fail-closed）。coordinator 实例 id 为进程内单调计数（运行期身份，不入科学文档）。
- `src/agent/harness/repair_capability_source.{h,cpp}` — **live capability adapter**：只读 `CapabilityKnowledge::instance()`（family default 合并后的 entry）→ `JsonRepairCapabilityProvider::buildFromCapabilityEntries`。不携带任何 operator id，删除/改价的条目自然消失——无第二 operator 表。编译进 `sicnu_agent`（knowledge 所在库），并令 `sicnu_agent` PUBLIC 链接 Qt-free 叶子 `sicnu_repair_planner`。

**修改**
- `src/agent_ops/recovery_bridge.{h,cpp}` — `projectRepairPlan(diag, ctx)` 重写为真规划：`repairFindingsFromDiagnostic`（sources 顶层 preflight issues，兼容 run() 路径的 sources["bridge"]["sources"]["preflight"]；**不**把 proposals 冒充 findings）→ `planRepairsForFindings`（provider 注入；autonomy 用与 decide 同一个 gate 推导，无第二真相）。不可诚实规划时输出 typed `no_safe_repair` 信封（no_provider / no_findings / invalid_findings）。`decide()` kRepair 分支：plan status 非 planned → ask `NO_SAFE_REPAIR_PLAN`；leading risk 改读**计划自身** selected[0].risk_class（caller claim 不能把 radiometric/science_changing 降级成 auto）；`requires_reverification=true` 随 kRepair 上 wire。
- `src/agent_ops/ops_types.{h,cpp}` — `RecoveryDecision.requiresReverification`（wire: `requires_reverification`）。
- `src/agent_ops/operations_coordinator.{h,cpp}` — Dependencies 增 `repairCapabilityProvider`；裸 bool 批准状态替换为：`armRepairApproval(token, nowMs)`（verify→armed；consumed digest 环形表 16 → replay 拒绝）、`hasPendingRepairApproval()`、`instanceId()`、`lastProjectedRepairPlanId()`（evaluateRecovery 记住其投影的 plan——run() 的 post-hoc decision 是 advisory ask 不带 plan，这是驱动的标准取计划路径）；run() 启动时对 request 直传 token / armed token 统一**再验证**，任何不可验证 → fail-closed 不武装 + `OpsRunResult.approvalError` typed 上报；armed 状态一次性消费。
- `src/agent_ops/session_surface.cpp` — `approve_repair`：无投影计划 → `NO_PENDING_REPAIR_PLAN`；args.plan_id 与投影不符 → `APPROVAL_WRONG_PLAN`；mint + arm，返回 token 文档。run/resume args：`repair_approval` + `approval_now_ms`；遗留 `approve_pending_repair` bool → 显式 `APPROVAL_TOKEN_REQUIRED` 拒绝（静默忽略即是诚实性缺口）。
- `src/repair_planner/repair_state.cpp` — `validRecord` 拒绝负 sequence（坏时钟不得静默跳到 eviction 队头；0 是合法单调序起点）。
- CMake：`src/agent_ops/CMakeLists.txt` +1 源文件；`src/agent/CMakeLists.txt` +2 源文件 + `sicnu_repair_planner` 链接（模块内最小 delta；已过 `test_build_wiring_drift`）。
- 测试：`tests/test_agent_ops_core.cpp`（+9 case / 1 case 契约更新）、`tests/test_repair_planner_completion.cpp`（+3 case）、`tests/test_capability_knowledge.cpp`（+1 live-adapter case，真文档）。

## 已知边界 / 未做

- token digest 为**tamper-evident**（仓内既有 discipline：planning state、agent_plan fingerprint 同级），不是防伪造的 MAC——持有者身份由 coordinator 绑定 + 一次性消费承担；跨重启的持久化消费记录不在本切片（journal 化属执行记录域，非计划域）。
- quality_mask finding 无上游生产者（preflight/harness_error 均无 mask 类 code）——按 Track 目标 5 **不发明**；其现有合法入口（radiometric 替代族）保持 #1278 既有测试覆盖。
- repair planner 仍为 planning-only；执行 seam（谁跑计划）属调用方，本 PR 只保证：修复 exit=0 ≠ 修复成功（requires_reverification 强制在 wire 上）。
