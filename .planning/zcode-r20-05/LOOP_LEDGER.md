# R20-05 LOOP_LEDGER

Worktree: `/home/kevin/projects/rs-studio/exp-rs-agent/r20-05-teaching-verification-20261022T190641Z`
Branch: `agent/r20-05-teaching-verification-20261022T190641Z`;BASE = origin/master `28e1891390`(fetch 实测)
会话上限:15 轮;全局构建锁:`main/.git/exp-rs-zcode-global-build.lock`(与 r20-01/02/03/04 串行)

| 轮 | 差距 | 聚焦改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|---|
| R0 | LIVE recon | fetch/PR(0 open)/issue(126 open,12 相关逐条读)/worktrees;sibling r20-01..04 在各自 worktree | git/gh 实拉 | 基线落定 BASELINE.md | 域盘点 |
| R1 | triage | 12 issue 逐条对 master 现码:10 已修(#1664/#1662),开放 3+#1652-1+CT-1 merge-loss(实证 c8f5e0805422 丢 r15-E 教学侧) | git diff base/side/today 三方核对 | ISSUE_TRIAGE.md | 修复 |
| R2 | WP-ML | 恢复 course_home 钳制+派生 / feedback indeterminate / loadBatchReportFromFile + dock 两调用点 / 2 测试文件(9+6 case)+ append 注册 | 待构建(锁队列) | commit 1a61058fc8 | WP-RB |
| R3 | WP-RB | validateProcessRubric 委托 GradingRubric::fromJson(strict reader + 错误映射);3 测试改钉引擎方言 | 待构建 | commit 20524dcbb6 | WP-SV |
| R4 | WP-SV | GridInfo.fractionsFromSample/sampledPixels;probe 标记;evalArtifactGrid 证据盖章(全分支);2 新测试 | 待构建 | commit 775772609e | WP-JP |
| R5 | WP-JP | ledger/verdict_stamp 解析 pin256+catch;嵌套炸弹回归 | 待构建 | commit 79adab3a33 | 构建 |
| R6 | 构建 | configure 排队(r20-03 构建中 → r20-01 → r20-04 → 本轨) | flock 队列实测 | 等待中 | 域闭包构建 |

## 关键决策记录

- **D1**:WP-ML 优先于 issue 修复 —— merge-loss 是带 git 考古实锤的"修复丢失",RED 证据最硬(stats 测试在 master 上可直接编译运行且红)。
- **D2**:#1652-1 采用委托(ONE rubric truth)而非在 admin 侧复制引擎规则 —— 仓库既有模式(lab_pack 委托);复制必然再漂移(正是 #1653 抱怨的形状)。
- **D3**:#1600 只盖章不改判定 —— probe 头文件明示采样是声明式有界观测契约;把采样估计降级 Indeterminate 超出 issue 证据,留给评审/后续。
- **D4**:#1601(双核)与 #1536(guided_workflow_widget)不修 —— 前者需 maintainer 架构决策,后者在 04 轨写区;均入 CROSS_TRACK_FINDINGS。
