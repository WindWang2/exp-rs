# R21-D TASKS（里程碑 → 原子工作包）

- [x] M0-A LIVE 盘点（git/PR/issue/文档/兄弟轨）→ BASELINE/ISSUE_TRIAGE/OWNERSHIP/PR_COLLISION/RISK_REGISTER
- [x] M0-B worktree 创建 + 构建配置
- [ ] M0-C 全量编译通过；记录峰值 RSS/时长
- [ ] M1-A R20 seam 回归三腿：test_raster_processing_dialog_base（cancel）、cartography raw schema registry 腿、test_layout_tools（DPI）
- [ ] M1-B CP11（test_cartography_production_11）同门复跑 → 3 红分类（环境/产品）
- [ ] M1-C FIXED-OPEN 复测证据：#1535/#1536/#1537/#1559/#1561/#1562/#1563/#1564 对应套件跑绿（存在套件的）
- [ ] M2-A **W1b**：RED 测试（facade stop/save 后 hasActiveEditLease/revision 断言）→ Edit Lease 协调器 seam（facade）→ window 实现 → GREEN
- [ ] M2-B W1b 边界：leaveEditable 重取 lease、commit 失败路径、无 coordinator 回退、重定位存活
- [ ] M3-A #1622 墙钟门槛改确定性断言；#1621 恒真断言补真实行为断言（UI 测试卫生）
- [ ] M3-B #1623 .qm install 规则（根 CMake append-only）+ 验证
- [ ] M4-A CommandRegistry/SelectionContext 状态转移审计补强（enabled-state 一致性断言表）
- [ ] M4-B 依 CP11 分类结果决定 D2 编图/导出纵深项
- [ ] M5 端到端用户路径测试覆盖核查（6 路径是否已有 offscreen 测试；缺口补测）
- [ ] M6 R1 独立评审 → 修复；R2 独立评审 → 修复
- [ ] M7 push + PR（不合并）+ FINAL_REPORT + PR_BODY

## 波次迭代记录

（每轮 goal-loop 在 LOOP_LEDGER.md 记账；连续 3 次无法推进 → 换假设；连续 8 次 → 审视前提。）
