# R19-02 OWNERSHIP — Pi/MCP Harness、工具契约、科学决策与 Workflow 闭环

基线：`origin/master = e12a9afbc8883a474dc9ffcaf3345ae55863dc71`（2026-10-20 经 `gh api repos/WindWang2/exp-rs/branches/master` LIVE 核对）。
Worktree：`/home/kevin/project/exp-rs-02-pi-mcp-workflow-20261020T102409Z`；分支 `agent/r19-02-pi-mcp-workflow-20261020T102409Z`。

## 本轨可写（写前仍需逐文件核对开放 PR 无冲突）
- `pi/**`（TypeScript Pi 扩展 + `pi/test/**` Node 测试；`pi/knowledge`、`pi/roles` 文档内容非目标，仅在工具面变化时同步）
- `src/agent/mcp_server.*`、`src/agent/data_platform_tools.*`、`src/agent/interaction_tool_registry.*`、`src/agent/workspace_snapshot.*`、`src/agent/workspace_state.*`
- `src/agent/tool_catalog/**`、`src/agent/spatial_tools/**`、`src/agent/harness/**`、`src/agent/commands/**`、`src/agent/contracts/**`、`src/agent/autonomy/**`、`src/agent/tools/**`
- `src/agent_loop/**`、`src/agent_ops/**`、`src/agentbench/**`
- `src/workflow/**`、`src/sdk/**`（含 `src/sdk/exprs/**`）、`src/plugins/**`
- `src/science_context/**`、`src/scientific_state/**`、`src/preflight/**`、`src/planner/**`、`src/repair_planner/**`、`src/contracts/**`、`src/explain/**`（explain 仅限实证的死成员/性能问题，B10 约束）
- 测试：`tests/test_mcp_*`、`tests/test_workflow_*`、`tests/test_agent_*`、`tests/test_surface_*`、`tests/test_preflight_*`、`pi/test/**`，以及本轨新增的独立命名测试文件（前缀 `test_r19_02_`）

## 本轨只读（默认不动；确需改动 → 最小专项修复 + 记录 CROSS_TRACK_FINDINGS）
- `src/processing/framework/task_center.*`（B07 观察/测试可写，核心实现改动需先证明未被锁定）
- `tests/CMakeLists.txt`、根 `CMakeLists.txt`、`.gitignore`、`CLAUDE.md`、`docs/adr` 索引、共享安装脚本 → 仅 append-only 最小注册本轨新测试目标，提交前与 origin/master 定向 reconcile

## 其他轨锁定（绝对只读）
- 轨 01：`src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`、`src/runtime/**`、`src/operators/**`、`src/processing/**`（算法部分）
- 轨 03：`src/agent/cartography/**`、`src/agent/mapspec/**`、`src/agent/layout_tools/**`、`src/agent/symbology/**`、`src/agent/*_dock_widget.*`、`src/agent/view_control_service.*`、`src/app/**`、`src/gui/**`、`resources/**`
- PR #1663（r18-a）写集：`src/analysis/segmentation/rs_multires_segmenter.*`、`src/operators/rs/{rs_mosaic_operator,rs_raster_spatial_operators,rs_sam_classify_operator}.cpp`、`src/processing/algorithms/{atmospheric_correction,chunked_processor,image_fusion,mosaic}.cpp`、`.planning/r18-a*`
- PR #1664（r18-e）写集：`src/teaching/**`、`src/teaching_admin/**`、`src/lab/**`、`src/grader/**`、`src/verification/**`、`src/faultlab/**`、`src/experiment_studio/**`、`src/cli/lab_batch_runner.*`、`src/help/help_content_store.cpp`、`data/faultlab/faults.schema.json`、`.planning/r18-e*`
- `src/verify/**` 与 `src/verification/**` 的统一：#1664 领域，只评估 ADR 0172 分歧、产出跨轨提案，不重构

## 共享机器资源
- 全局构建锁：`/home/kevin/project/exp-rs/.git/r19-global-j2-build`（三轨共用，resource_guard.py --wait 3600）
- `CMAKE_BUILD_PARALLEL_LEVEL=2`、`ctest -j1`、`QT_QPA_PLATFORM=offscreen`、构建目录 `build-r19-02`（本轨独有，绝不复用他轨）
- 他轨 worktree/分支/构建目录/PR 一律不删不改；禁止 `git clean -fdx` 越界

## 候选任务登记（path → upstream PR → master blob SHA → reproducer → owner）
见 TASKS.md（逐项落列，随轮次更新）。
