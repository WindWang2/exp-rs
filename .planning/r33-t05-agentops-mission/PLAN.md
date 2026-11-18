# R33-T05 PLAN — 首批工作包闭环

首批范围（证据完备、相互独立、最小闭环）：WP02 / WP05 / WP06 / WP08 / WP09 / WP11 / WP13。
其余 WP（WP03/04/07/10/12/14–20）按剩余增量与时间推进，已修候选凭证据退场。

## 实现顺序与验证

1. **基线**：configure dev-default → 构建受影响目标 → 跑既有相关 CTest（green 基线）。
2. **WP02** mission_timeline_model appendNewTasks 契约修复 + 新测试（QAbstractItemModelTester、persistent index、分页可达性）。
3. **WP05+WP06** session_journal：seq 极值/next_seq 采用/append 拒绝 + compaction 保 terminal stopReason + 新测试。
4. **WP08+WP09** mission 三读路径有界读 + last-good 同字节绑定 + 新测试（超限/损坏/半写/A→B 竞争）。
5. **WP11** secret_redactor fault 子树脱敏 + 新测试（synthetic fault.api_key、嵌套 authorization、长描述、科学 digest、合法 string marker 精确保留）。
6. **WP13** delivery_assembler 未知 schema fail-closed + claims source 标记 + 新测试；确认旧测试不回归。
7. **收尾**：`git diff --check`、受影响旧套件全跑、R1 独立 review（子 agent 读 BASE..HEAD diff）→ 修复 → R2 复审 → 刷新排他 → push + PR。

## 受影响构建目标（待 -N 核实后回填真实 target 名）

- 既有：test_agent_loop_core / test_agent_loop_journal_durability / test_mission_context / test_mission_runtime_store / test_mission_timeline_surface / test_mission_scale_benchmark / test_agent_ops_core（凡存在者）
- 新增：test_r33_t05_mission_paging / test_r33_t05_journal_bounds / test_r33_t05_sidecar_recovery / test_r33_t05_redaction_delivery（命名以最终注册为准）

## E2E 映射

- E2E-01 ← WP02+WP03/04（首批覆盖 WP02 腿）
- E2E-02 ← WP05+WP06（+WP07 后续）
- E2E-03 ← WP11+WP13（+WP12/14 后续）
- E2E-04 ← WP08+WP09（+WP10 后续）
- E2E-05 ← WP16+（后续批次）
