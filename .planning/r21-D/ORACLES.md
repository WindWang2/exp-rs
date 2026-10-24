# R21-D ORACLES（硬性完成判据）

## 轨内 Oracle

- O-D0 `hasActiveEditLease` 一致性：任何 UI 入口（主窗口 toggleEditing/saveEdits 与属性表 facade saveEdits/toggleEditing/saveEdits()）终止编辑会话后，DataManager 侧 Edit Lease 状态与 UI 编辑状态一致（无孤儿 lease、无假锁）；commit 路径 revision 前进。RED（master 实跑红）→ GREEN×2。
- O-D1 R20-04 三 seam 回归绿：cancel（test_raster_processing_dialog_base 全家）、raw schema registry 腿、DPI（test_layout_tools 全家）。
- O-D2 CP11 3/28 红：同门复跑结果与 R19-03/R20-04 记录逐位比对；分类结论写入 TEST_RESULTS（环境红需解释机制；产品红则修）。
- O-D3 测试卫生：#1621 恒真断言改为可失败断言且断言真实行为；#1622 墙钟门槛改确定性判据；两处 RED-GREEN 或行为等价证明。
- O-D4 .qm install：`cmake --install` 演练（或构建树内等价验证）证明 sicnu_zh_CN.qm 进入安装布局。
- O-D5 CommandRegistry/SelectionContext：状态转移断言（selection/layer/task in-flight 组合下命令可用性）存在且绿；新增断言与既有 1661/1665 契约一致。

## 通用 Oracle

- O-G1 基线盘点证据在 BASELINE.md（SHA/PR/issue 全量）。
- O-G2 每项修复 RED→GREEN×2（两轮独立运行 exit 0），边界反例至少 1 组。
- O-G3 无 golden 快照篡改；无 TODO 假实现；无 skip 掩盖。
- O-G4 R1/R2 双独立评审，P0/P1/P2 清零。
- O-G5 PR 文件变更 100% 在 OWNERSHIP 白名单内；历史 merged patch 无回滚（merge-loss 审计）。
- O-G6 未执行项诚实标注 NOT_EXECUTED + 原因。
