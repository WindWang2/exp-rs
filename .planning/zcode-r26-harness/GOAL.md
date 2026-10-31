# R26-03 GOAL — Pi/MCP 空间智能 Harness 可靠闭环

任务 ID: harness | 分支: agent/r26-harness-20261030T151107Z | 基线: origin/master=2610cb4741e69a89124921e44c03e340e2522ee2

## 目标
深化既有 Pi+MCP+ToolCatalog+TaskCenter+Workflow+Harness 的可靠性：
多轮连续任务上下文真值、能力选择可解释、工具正确调用、失败恢复、可审计结果。

## 完成条件（Oracle）
1. 选定工作包：RED（修复前真实实现可复现失败）→ 最小修复 → GREEN×2（含回归变异验证）
2. R1/R2 独立评审（不同上下文子代理），P0/P1=0
3. 本地构建（-j4 上限 + flock）与定向 ctest 通过
4. gh pr create 成功，PR URL 记录于 FINAL_REPORT
任何硬门失败 → BLOCKED_NOT_DONE 如实记录。

## 写区
独占：src/agent/harness/、src/planner/、src/agent_ops/、src/agent_loop/、src/sdk/exprs(执行契约子模块)、src/plugins/framework|host(安全缺口)、tests/、docs/agent/
只读：#1727 (pi/mcp_bridge.ts, pi/exp-rs-spatial.ts, src/agent/mcp_server.*, src/workflow/workflow_run_coordinator.*, tests/surface_diff_snapshot.json)
      #1726 (src/data/import_preflight.*, src/operators/runtime/inference_plan.*, src/experiment/replay_deviation.*)
