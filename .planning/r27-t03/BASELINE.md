# R27-T3 Baseline — Pi/MCP Agent、审批权限、工具安全与 SDK/插件闭环

- BASE_SHA: `10652fd003c66ae78598d2b878eed9beb50029fa` (origin/master, fetched 2026-11-02T13:24Z)
- Worktree: `../exp-rs-r27-t03-20261102T132444Z`, branch `agent/r27-t03-agent-security-sdk-plugins-20261102T132444Z`
- 无飞行中 PR（最后合并 #1731 = base）；无活跃兄弟分支触碰本轨文件（冲突矩阵空）
- Host: Linux 6.18 cachyos, 16 cores, 62GB RAM；编译 -j2 + flock /tmp/exp-rs-host-build.lock
- 构建: cmake preset dev-default (Debug, ENABLE_TESTS=ON), build dir `build-dev/`（worktree 内隔离）

## 审查来源
2026-11-01 多路审核产出 #1732–#1806（base 即审核 HEAD）。本轨 13 个 issue 逐一在 base
源码复核：全部 LIVE（无一 FIXED_IN_MASTER）。证据见 ISSUE_TRIAGE.md。

## R25/R26 已完成（不重做）
- #1727 (r25-C): Pi/MCP harness、工具契约、capability honesty
- #1729 (r26-harness): evidence ledger fail-closed、planner 事实门、capability family 漂移治理
  （tests/test_r26_{harness_evidence_integrity,planner_fact_gates,capability_family_drift}.cpp 已存在）
