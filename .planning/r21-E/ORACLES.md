# R21-E ORACLES — 硬性完成判据

## 域 Oracle
- **O-E1** 学生全流程：既有实验包 → 真实数据 → operator → 成果 → 提交 → 评分 → 可解释反馈；PASS/FAIL/UNKNOWN 与存储证据一致（W6 端到端实测 + verifier 证据三值语义测试）
- **O-E2** 批量：重跑/重复导入/一半失败/无权限/中断恢复精确计数幂等；100–1000 输入压力 + 峰值 RSS 前后测试（W5）
- **O-E3** verification/verify/adapter/FaultLab 契约隔离可解释、生产调用有测试；bad pack/畸形 JSON/采样歧义不得「可信通过」（W1/W2 回归 + 既有 faultlab 对抗测试保持绿）
- **O-E4** R20 #1668 关键回归（merge-loss/rubric/sampling/深 JSON/ledger）两遍绿；#1558/#1598/#1599/#1602 master 复测证据落 BASELINE（✅ 已完成）
- **O-E5** 离线零 egress、包 hash 验证、成绩与证据报告可复现（Linux 实测；Windows 侧 NOT_EXECUTED 分清）

## 通用 Oracle
- **O-G1** master LIVE 盘点 + 126 issues 再分诊 + 冲突矩阵（✅ BASELINE/ISSUE_TRIAGE/PR_COLLISION_MATRIX；提交前复查）
- **O-G2** 每项修复 RED→GREEN×2、边界反例、生产路径证明（W1.1/W2.1… 驱动文件 + RUN_LOG）
- **O-G3** 性能基准与基线同条件（W5 记录吞吐/RSS/耗时）
- **O-G4** 受影响测试 + 两个独立 reviewer PASS（M6）
- **O-G5** 残余 red/skip/NOT_EXECUTED 逐项诚实归因（TEST_RESULTS.md）
- **O-G6** PR 文件 100% 白名单内；历史 merged patch 无静默回滚（R20-05 43 文件清单 diff 对照）
- **O-G7** 无 TODO 假实现、无占位 mock 顶替生产、无绕门、无假 PASS
