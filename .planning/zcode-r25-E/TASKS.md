# TASKS — R25-E 工作包

| WP | 内容 | 文件（E 所有权） | 状态 |
|---|---|---|---|
| W0 | 侦察/基线/triage/冲突矩阵 | .planning/zcode-r25-E/* | DONE |
| WP-E1 (W5) | CT-C6+#1695-17c 残留：discovery 取消钩子 + preDiscovered 单扫描 seam + 析构 join 有界化 | src/teaching_admin/batch_assessment.{h,cpp}、src/app/teaching_admin/teaching_admin_dock.cpp | TODO |
| WP-E2 (W6/CT-D1) | run_lab.cmd foundry 权威一行修复 + 脚本契约守卫测试 | scripts/windows/run_lab.cmd、tests/test_r25_e_* | TODO |
| WP-E3 (W1) | #1695-17d 空白自由作答拒绝（record + reload 双边界） | src/lab/session_state.cpp、tests | TODO |
| WP-E4 (W3) | #1653 beginResult 三重复制去重 | src/verification/checks_*.cpp | TODO |
| WP-E5 (W2) | rubric canonical schema 一致性审计+防漂移回归 | tests（视现有覆盖） | TODO |
| WP-E6 (W2/W4) | 反馈包可解释证据块（topDeduction/graderDigest/unavailableReason 上桌） | src/teaching_admin/feedback_pack.cpp | TODO |
| WP-E7 (W7) | 双场景 E2E + 对抗矩阵（跨学生/深JSON/NaN/缺工件/路径穿越/损坏/cancel后恢复） | tests/test_r25_e_* | TODO |
| W8 | R1/R2 独立评审、push、PR | — | TODO |

## 深化映射（Oracle #2 的 3 项）
1. LabSpec/步骤操作：WP-E3（作答卫生：record/reload 双边界拒空白）
2. 评分/科学验证：WP-E6（评分证据可解释化：行内既有权威证据进反馈包）
3. 教学管理 UX：WP-E1（单扫描 + 取消即时生效 + 关闭不挂起）
