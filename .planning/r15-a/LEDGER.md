# R15-A LEDGER — Oracle-Ledger 循环记账

> 循环：分析 → RED → 最小修复 → GREEN×2 → 记账 → 下一任务。连续三轮无进展须改变假设；八轮无进展回查前提。每阶段可恢复 checkpoint。

| # | 任务 | Oracle | 状态 | 证据 |
|---|---|---|---|---|
| L0 | M0 事实冻结（BASELINE/OWNERSHIP/矩阵/ORACLES） | — | DONE | origin/master=46791bc6；0 open PR；#1505/#1509/#1512 已合并；25 issues 分类完成 |
| L1 | 构建树 build-r15-a 配置 + 9 个优先目标编译通过 | — | IN_PROGRESS | configure_r15a.cmd；待记录 exit code |
| L2 | O3：#1465 3-arg std::equal 定位与分类 | O3 | PENDING | — |
| L3 | O5：rs_spectral_index #1509 diff 审计 + 回归基线 | O5 | PENDING | — |
| L4 | O1：融合 resample-then-mask 边缘掺混 RED→GREEN×2 | O1 | PENDING | — |
| L5 | O2：sam/spectral_similarity NoData 方言双锁或统一 | O2 | PENDING | — |
| L6 | O4：#1454 科学算子回调侧审计 | O4 | PENDING | — |
| L7+ | A01–A16 工作包审计新发现（按复现准入排队） | O6+ | PENDING | — |

## 阻塞与假设变更记录

（暂无）

## Checkpoint 索引

- CP0（本轮）：M0 完成，分支 `hardening/r15-A-science-processing` @ 46791bc6 + M0 文档提交。
- 下一 session 恢复点：读本文件 + `.planning/r15-a/BASELINE.md`，从 L1 继续。
