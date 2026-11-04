# R27-T05 Final Report

- **Base SHA**: `10652fd003c66ae78598d2b878eed9beb50029fa`（= push 时 origin/master，无漂移）
- **Head SHA**: `b96321f7fc…`（13 commits，58 files，+2.6k/−0.6k）
- **PR**: https://github.com/WindWang2/exp-rs/pull/1809（只发不合）
- **Worktree**: `../exp-rs-r27-t05-20261102T163500Z`（本轨唯一，全程 flock+≤-j2）

## 本轨实际处理 Issues（26）

FIX（24）：#1734 #1735 #1736 #1737 #1744 #1748 #1749 #1750 #1768 #1770 #1771 #1772 #1773 #1774 #1775 #1776 #1777 #1778 #1779 #1780 #1781 #1782 #1783 #1784 #1785 #1786 #1787（含 #1785/#1786）——共 27 个编号（#1772 计入）。
核实 FIXED_IN_MASTER（2）：#1697 #1698（证据在 ISSUE_TRIAGE）。
REDIRECT（1）：#1769（src/agent_loop 归属 agent 轨）。
AUDIT（1）：#1732（API 实证无保护 + RULESET_PROPOSAL.json 可执行提案；刻意未实施，D2）。
PARTIAL 记账：#1751/#1699/#1682/#1683（理由与范围见 TRIAGE）。
**P0 计划外**：tests/CMakeLists.txt 追加区合并错切重建（master 测试通道全断的根因）。

## 验证（命令 ×2）

- 关键 oracle 12 套件评审后双跑 **逐字节一致全绿**（81/25/34 新 oracle；14716/216/34/3473 漂移门；587/101/320/57/484 回归）。
- 全量验证轮 23 套件 ×2 一致（含 36107 断言 ep8；其 bucketed-queue 1 用例 BASE 对照实证 PRE-EXISTING-RED，非本轨回归）。
- RED：grader 族回退 BASE → 28/95 断言失败实录；matrix 重放遗漏被 oracle 当场抓红。
- 退出码：全部运行经 ctest/直跑，失败即非零（RED 证据）。

## R1/R2

R1 4 findings（P1 UB 修复/P2 修复/P3×2 记录）；R2 2 findings（P1 流程假绿修复/P3 rot 防护修复）。子代理配额受限 → 主代理内联双视角评审（声明于 REVIEW_*.md）。

## 性能

无性能声称（正确性轨）；BENCH_BASELINE 记录构建事实与资源峰值。

## 未测与外部

Windows/macOS：逻辑静态核对，NOT_EXECUTED_LOCAL。远程 CI 不等待（轨规则）。ruleset 未实施（见 D2）。GCC 偶发 ICE（3 次）均以重跑收敛，非代码错误；由此衍生的"断言计数必须随补丁变化"流程教训已入账（R2-F1）。

## 白名单核验

`git diff --name-only base..HEAD` 全部落于声明范围；白名单外定向路径（src/repair_planner、src/processing/framework/task_center.cpp、docs/generated/help、data/contracts、.gitignore 追加块）均在 PR 显著声明并给出依据（DECISIONS D1/D12、#1737 fixture 性质、每轨 allow-list 先例）。
