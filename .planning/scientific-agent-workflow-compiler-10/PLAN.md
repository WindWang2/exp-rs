# PLAN — work packages & phase mapping

| WP | Package | Phase | Files |
|---|---|---|---|
| WP1 | WorkflowIR core (reader/normalize/fingerprint/bounds/facts) | 1-2 | workflow_ir.{h,cpp}, test_workflow_ir.cpp |
| WP2 | Static analysis (18 checks, typed issues) | 3 | workflow_analysis.{h,cpp}, test_workflow_analysis.cpp |
| WP3 | Repair table (rules, risk classes, records, refusals) | 3 | workflow_repair.{h,cpp}, test_workflow_repair.cpp |
| WP4 | Planner pipeline + harness:compile_workflow | 4 | workflow_planner.{h,cpp}, test_workflow_planner.cpp |
| WP5 | Context checkpoint/resume/compaction + harness:workflow_session | 4 | context_checkpoint.{h,cpp}, test_context_checkpoint.cpp |
| WP6 | Attempt ledger + repeated-error guard in diagnose_run | 4 | run_loop.cpp, context_ledger.{h,cpp} |
| WP7 | Tool shortlist + budget report + harness:tool_shortlist | 4 | tool_shortlist.{h,cpp}, test_tool_shortlist.cpp |
| WP8 | Pi bridge dedup + F-PI-1/2 + drift test | 5 | pi/*.ts, pi/test/* |
| WP9 | Eval corpus extension + explain IR projection | 5-6 | data/agent/evals/cases/*.json, tests |
| WP10 | Review (2 subagents) → fixes → final verification → PR | 7-9 | REVIEW_LOG, EVIDENCE, PR_BODY |

## Execution order & budget (300M tokens)
0 基线/规划 18M · 1-2 IR 核心 54M · 3 分析+修复 54M · 4 planner/context/loop/shortlist 48M ·
5 pi+eval 34M · 6 边界/规模验证 20M · 7 review 24M · 8 修复 20M · 9 PR 8M — 合计 280M,
留 20M 缓冲 (goal-template 允许工作包合并重算; 0/6/7/8 占比保持)。
