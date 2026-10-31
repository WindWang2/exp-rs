# OWNERSHIP / 防撞账本
本轨写区（全部经 worktree agent/r26-harness-20261030T151107Z）：
- src/agent/harness/plan_tools.cpp、evidence.{h,cpp}、context_checkpoint.cpp、context_ledger.cpp、capability_catalog.cpp、harness_error.cpp
- src/planner/planner_core.cpp
- tests/test_r26_*.cpp（新增）、tests/CMakeLists.txt（仅尾部 BEGIN/END R26-harness 块）
- .planning/zcode-r26-harness/

只读边界（不触碰）：
- #1727: pi/mcp_bridge.ts、pi/exp-rs-spatial.ts、src/agent/mcp_server.*、src/workflow/workflow_run_coordinator.*、tests/surface_diff_snapshot.json、pi/test/*（其新增）
- #1726: src/data/import_preflight.*、src/operators/runtime/inference_plan.*、src/experiment/replay_deviation.*
- 兄弟轨 tests/CMakeLists.txt 尾部块（R23-B/R24-A/B/C）保持原样，只在文件末尾追加
