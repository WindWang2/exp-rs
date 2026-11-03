# R27-T05 Tasks

- [x] Preflight（BASELINE/ISSUE_TRIAGE/PR_COLLISION_MATRIX/DECISIONS）
- [x] P0 tests/CMakeLists.txt 追加区重建（configure 恢复）
- [x] #1734/#1735/#1736 P1 测试通道
- [x] #1744 grader 窗口（oracle 81 断言）
- [x] #1773 requiredStages 门禁（oracle）
- [x] #1775/#1776/#1779/#1780 workflow/dispatch
- [x] #1770/#1771 lineage/checkpoint
- [x] #1768/#1781/#1782/#1785/#1786/#1787/#1772 experiment 批次
- [x] #1783/#1784 bundle import TOCTOU/硬链接
- [x] #1774/#1778 repair_planner
- [x] #1777 grader CJK/否定
- [x] #1748/#1749/#1750 发布质量
- [x] #1732 ruleset 审计+提案（不实施）
- [ ] #1737 契约漂移测试修复（构建后）
- [ ] oracle 双跑 + TEST_RESULTS.md
- [ ] R1 独立评审 + 修复
- [ ] R2 独立评审 + 修复
- [ ] PR 提交

## REDIRECT / PARTIAL 记账

- #1769 → src/agent_loop 白名单外（agent 轨），REDIRECT。
- #1751/#1699 → 505 sleep 遍布 136 文件：只修与本轨直接相关且白名单内热点，其余如实 PARTIAL。
- #1682 → Win/mac 已带 guard/install 冒烟（#1708 时期），全量测试受 360 分钟上限（文件内论证）；本轨不再扩 CI 面。
- #1683 → 顶层 permissions: contents: read 已在 master（FIXED_IN_MASTER 部分）；其余子项 P3 记账。
- #1697/#1698 → 核实 FIXED_IN_MASTER（PR #1708；ci.yml 冒烟 + 根 CMakeLists INSTALL_RPATH 段）。
