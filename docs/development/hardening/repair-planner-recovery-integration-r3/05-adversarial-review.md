# 05 — Adversarial Review round 1 → fixes

独立 reviewer（subagent，读全量 diff + 实证探针）裁决：P0×1、P1×2、P2×5、P3×5。全部 P0/P1/P2 已修复并补 oracle；P3 处理如下。

## P0-1 science gate 只读 selected[0]（实证：CRS+radiometric 混合计划无审批直接 PROCEED）

**修复**（recovery_bridge.cpp decide()）：gate 改为扫过**全部** selected 候选取最大风险——`any(selected.risk_class ∈ {radiometric, science_changing})` 即整体 ask。计划是整体执行的，一个 radiometric 候选即整次发射需审批。
**Oracle**：`a mixed-risk plan is approval-gated as a whole`（RED 不可重现——原实现直接判 shape_preserving；新测试钉死 selected[1]=radiometric → ask → 绑定批准后整体 proceed）。

## P1-2 token 绑定旧计划，未约束本次授权的计划

**修复**：绑定键从 plan_id 改为 **findings digest**（plan 指纹含 `science_change_approved` 注记，批准后重投影会变 id，绑定 plan_id 会自锁；findings digest 是同一修复科学的稳定身份）。bridge decide() 在授权点强制 `ctx.approvedFindingsDigest == plan.provenance.findings_digest`，不匹配 → ask + `approval_error=APPROVAL_WRONG_PLAN`；裸 bool（无 digest）永不满足 gate。
**Oracle**：coordinator 生命周期 case 的 cross-plan 段（GRID 批准 → INVALID_RADIOMETRY → ask + APPROVAL_WRONG_PLAN）+ bridge 级 wrongBinding 断言。

## P1-3 唯一能到 repair 分支的路径接受未验证 bool

**修复**：`evaluateRecovery()` 改为**消费** armed token（verify：本 coordinator + 投影 digest + `ctx.approvalNowMs`）并**派生** `ctx.humanApprovedRepair/approvedFindingsDigest`——调用方 bool 永不被信任；拒绝时 `decision.approvalError` typed 上报且 gate 保持关闭。方法改 non-const（其语义本就变更状态）。头文件文档更新为驱动协议。
**Oracle**：flow 段——第二轮 evaluateRecovery 以 `humanApprovedRepair=false` 起步，token 验证通过后 proceed；拒绝路径返回 APPROVAL_WRONG_PLAN。

## P2-4 approvalError 不上 surface wire → 已修（sessionSurfaceStatus 增 `approval_error`；RecoveryDecision 增 `approval_error` 字段）。
## P2-5 severity 缺失的极性 → 保持 fail-closed abort（**命名自身却无法定级的证据绝不静默丢弃**；可规划的兄弟 finding 不构成豁免——typed no_safe_repair/ask 把人请回来）。极性写入注释并加测试钉死。
## P2-6 findings 洪泛 → bridge 侧 `kMaxBridgeFindings=1024` 硬上限（先截断后规划，digest 覆盖截断后的集合；planner 层 64-requirement 预算仍可见截断）。Oracle：4000-issue 洪泛 → 计划照常、确定性保持、`requirements_truncated=true`。
## P2-7 陈旧投影遮蔽 + const 写状态 → finish() 刷新/清空 `mLastProjectedFindingsDigest`（每次 run 后绑定目标总是最新投影）；evaluateRecovery 改 non-const。
## P2-8 缺失 oracle → 新增：混合风险 P0 探针、跨计划批准、direct-token（armed 路径）、run 后 stale-digest 拒绝、token 拒绝上 wire、PAUSED 前不消费（arming 状态保持，PAUSED 早退在消费之前——语义钉在测试注释）。

## P3 处置

- P3-9 消费环 16 上界：头文件注释写明（最旧遗忘；遗忘 token 重投仍被 expiry/arm 绑定拒绝）。
- P3-10 resume 忽略 token：改为 typed 拒绝 `APPROVAL_TOKEN_REQUIRED`（resume 不评估 recovery，无法诚实消费）；run args 中非对象 `repair_approval` → `APPROVAL_MALFORMED`。
- P3-11 adapter 懒加载全局单例：与所有消费方同路径（ensure-loaded）；未配置目录 → 空 knowledge → 全部 `no_safe_repair/no_candidate`（fail-closed）。头文件注明；不改全局语义（那是 CapabilityKnowledge 自己的目录策略）。
- P3-12 direct token 失败 + armed token 成功的并存：保守极性保留（approvalError 记录 direct 拒绝，armed 仍生效）——文档化。
- P3-13 旧状态文档含负序 → fromJson 拒绝（fail-closed，PR body 注明 release note）。
- P3-14 no_safe_repair 信封补 `planning_only=true`；`05-adversarial-review.md` 本文件即补上；isHex16 双份是叶子隔离的刻意代价（repair_planner 不依赖 agent_ops），保留。

## 复审

修复后全套件：test_agent_ops_core 34 cases / 326+ assertions 全绿；test_repair_planner_completion 34 cases 全绿；test_capability_knowledge 除**既有** D8 sidecar 漂移（rs:temporal_decompose，#1244 引入、旧 master 同样失败，非本 PR 范围）外全绿；wiring drift / schema 全绿。
