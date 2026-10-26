# R22-05 TASKS

| ID | 内容 | 状态 | 来源 |
|---|---|---|---|
| W0 | 盘点 + worktree + 规划文档 | done | 任务书 §1/§2 |
| W1 | #1695-1 async runner 生命周期：QPointer 父指针、析构恢复 override cursor、QPointer 守卫一致性；新测试 tests/test_r22_workbench_async_runner.cpp | pending | #1695（动态验真 OPEN_UNFIXED） |
| W2 | 本域主动审计（E2/E3/E5/E6）：dialogs/panels/widgets/display/editing/map_tools/layout 目录 live 缺陷挖掘（并行只读扫描 → 本轨人工实证 → RED→GREEN×2） | pending | 任务书 E2/E3/E5/E6 |
| W3 | E6 i18n/平台差异专项（非 teaching_admin 域内）：复数拼接、中英提示一致性抽验 | pending | 任务书 E6 |
| W4 | （视 W2 结果）E5 交互能力增量：现有面板/工作流的小步可验证增强 | pending | 任务书 E5 |
| W5 | R1 独立 review（fresh subagent，反对式） | pending | 任务书 §5.2 |
| W6 | R2 独立 review（另一 fresh subagent，复核 R1 修复） | pending | 任务书 §5.4 |
| W7 | 交付文档 + push + PR | pending | 任务书 §5.5-5.9 |

## 里程碑

- M0 盘点/分诊（本文件 + ISSUE_TRIAGE/PR_COLLISION_MATRIX）
- M1 W1 修复闭环
- M2 W2-W4 审计驱动修复闭环
- M3 双评审 + 交付

## 不做（并记录理由）

- E1 双窗口集成测试、E4 Atlas 覆盖恢复：BLOCKED_ON_#1677（符号只在未合并分支上）
- #1695 第 2/3/5 项、#1678/#1688/#1689/#1694：白名单外（CROSS_TRACK_FINDINGS）
- #1557：master 已修（FIXED_OPEN）
- #1632/#1638：已修 / fork 维护债不在本轨最小闭环内
