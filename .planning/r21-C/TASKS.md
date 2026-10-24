# R21-C TASKS — 里程碑与工作包

M0 基线 → M1 预防性测试/契约 → M2 修复 → M3 性能 → M4 功能深化 → M5 E2E → M6 审查 → M7 交付。

## M0（已完成 2026-10-23）
- [x] LIVE 盘点：origin/master=911e0b17a3（gh api 双确认）、Open PR=0、Open Issues=126、worktree 建立、vendor 配方 configure exit 0。
- [x] R20-03/R19-02 交付深读 + 反回滚清单。
- [x] 种子 issue 现码分诊（ISSUE_TRIAGE.md A/B/C/D 节）。

## M1 预防性测试/契约基线（进行中）
- [ ] 45 测试目标构建（/tmp/r21c-build-targets.txt，锁内排队）。
- [ ] R20 #1670 回归两遍绿（exprs_ipc/capability_knowledge/harness_evals/agent_ops_core/surface_parity/surface_e2e/mcp_server 族）。
- [ ] FIXED-OPEN 复测：#1551/#1549/#1588/#1595 主体（plugin 族/science_context 族/preflight_runtime_adapter/cli 族）。
- [ ] W8-red 重跑：plugin 5 二进制 master 同构红确认 + 失败模式归档。
- [ ] 抽查 #1594/#1572 现场。

## M2 修复波次（RED→GREEN，每包原子提交）
- **W-A #1597 残留** [已写码] pipeline run/validate/resume、workflow run/validate/list-runs/resume、catalog export、data catalog search 的 unknown-flag typed InvalidInput 拒绝（rejectUnknownCliFlag 四元组）。
- **W-B #1595 残留** [已写码] `--max-items` 数值 ok 门。
- **W-D9 CT-B** [已写码] `--repair-approval <token.json>`（1MiB 有界读+isObject 门）+ `--approval-now-ms` 通道；bare `--approve`/`--approve-pending-repair` parse 期 typed 拒绝并指路。
- **D5** [已写码] mLastKnownPid atomic write-once 快照；workerPid()/killProcess 败者/EOF probe 三读点转换；死后 diagnostics 仍点名死 worker pid。
- **D6** [已写码] workflow_run_lock boundedOwnerRead 64KiB；capability_mirror 4MiB cap+1。
- **W-E1 [P1] F1** [RED 已提交 b8c8c20a4b] m_cancelledRequestTasks 加 submitting-tool 门（execute_algorithm/execute_operator），镜像 R14-C FIX-1 的 pipeline 门——status 轮询被 abort 误杀执行中的任务。
- **W-E2 [P2] F2** [RED 已提交] preflight_algorithm/execute_algorithm/execute_operator 的 parameters 非 object typed INVALID_PARAMETER 拒绝（镜像 run_workflow pipeline 分支）。
- **W-E3 [P3] F3** scientific_agent_session 终态后 run() 不同 goal 毒化 journal（duplicate terminal event）——评估修复（terminal 检查前移）。
- **W-E4 [P3] F4** pi/exp-rs-spatial.ts poll_interval_ms 钳制下限（busy-loop 风暴）。
- **W-E5 [P3] F5** Pi 桥默认 category 列表 vs surface_registry 34 前缀漂移（20 家族静默丢弃）→ 由 tools/list 实际家族推导或至少报告丢弃。
- **W8-red** [待构建重跑] plugin 诊断码 5 二进制红归因→修复。
- 对抗性新扫第二波（TaskCenter 重复 terminal event、reload cache 并发）——R1 审查前视余量。

## M3 性能
- [ ] 受影响路径的基准（CLI 参数解析、capability reload、plugin 宿主回收）与内存上界记录；与 master 同条件对比。

## M4 功能深化（2026-10-24 裁剪决策）
- **#1628-3 temporal phenology 三工具接线：维持 DEBT**（R19-02/R20-03 两轮同判）。现场复核：工具已实现且有 schema/dispatch/测试（spatial_tools/temporal_spatial_tools.cpp:99-130,210-260；test_d16_temporal_tools），但接线=新增 MCP 有状态会话面（ingest_series 的 static store 生命周期、并发语义、三腿 sidecar/docs 对账），非机械桥接；本轨余量优先投给 F1/F2 实证修复与 W8-red。
- **#1596 flag 解析器收敛：维持 DEBT**（纯重构；W-A 已把缺陷面——未知 flag 静默吞掉——统一消除，六个解析器的行为契约现在一致由 RED 网守护）。
- **W-E5 Pi 桥 category 漂移：判定非缺陷撤销**——surface_snapshot.test.mjs 已把每个未桥接家族记为有意的决策矩阵（三角形防漂移门），F5 为误报。

## M5 E2E
- [ ] ≥4 条 E2E：discover→inspect→plan→run→status→cancel/resume→verify（真 Pi extension + 真 server/CLI；插件拒绝/坏 schema/权限撤回场景）；两轮。
- [ ] schema sidecar/手册再生 diff 清洁。

## M6 审查
- [ ] R1 fresh reviewer（独立上下文）：diff 全读 + 反回滚 + 断言强度 + 对抗边界。
- [ ] R2 第二 fresh reviewer：含实际执行测试；循环至 P0/P1/P2 清零。

## M7 交付
- [ ] PR_BODY.md、push 分支、`gh pr create`（不合并、不等 CI）；FINAL_REPORT.md；Oracle checklist。
