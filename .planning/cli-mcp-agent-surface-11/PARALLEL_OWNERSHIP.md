# PARALLEL_OWNERSHIP — cli-mcp-agent-surface-11

基线：`origin/master@a5b11b7f10`（2026-09-16 审计）。

## Open PR × 本 track 文件交集

### PR #1008 `zcode/radiometric-spectral-workbench`（spectral 域，DIRTY）
- 触碰 `src/agent/CMakeLists.txt`（+3 行 spectral sources）、`src/agent/spatial_tools/spatial_tool.cpp`
  （+4 行注册）、`src/agent/spatial_tools/spectral_spatial_tools.*`（新文件）。
- **本 track 策略**：不写 `src/agent/spatial_tools/**`；本 track 对 `src/agent/CMakeLists.txt`
  的修改若发生，保持 append-only（tool_catalog 新源文件），冲突面 = 各自 append 行，rebase
  可平凡收敛。spectral 工具经 SpatialToolRegistry 注册后自动进入 catalog 投影——
  本 track 的投影/gate 天然覆盖它们，无需复制任何功能。

### PR #1009 `zcode/execution-runtime-convergence-11`（执行运行时域，UNSTABLE）
- 触碰 `src/agent/data_platform_tools.cpp`（+4 行 include）、`tests/CMakeLists.txt`（+20 行）、
  `CHANGELOG.md`（append）、`.gitignore`（append 4 行）、`data/help/diagnostics.json`、
  `src/runtime/**`、`src/operators/framework/**`、`src/processing/framework/{fused_chain,
  local_worker_pool}.*`、`src/workflow/pipeline_run_coordinator.cpp`、大量 tests/test_*11。
- **本 track 策略**：
  - `data_platform_tools.*` 视为 read-only，只 include 消费；
  - `tests/CMakeLists.txt` 只 append 新测试目标行；
  - `CHANGELOG.md`/`.gitignore` 推迟到最后 integration commit，append-only；
  - `src/runtime/**` 不触碰（其 execution_telemetry/governor 属该 PR 领地，本 track 进度协议
    只依赖 master 已有的 TaskCenter 回调 seam）。

## Open issues × 本 track

#1001–#1007 全部为域语义缺陷（io/workflow/dataset/georef/agent 数据工具），与本 track 的
协议/surface 职责零文件交集与零功能重叠 → 全部 OUT_OF_SCOPE（EVIDENCE 登记）。

## 本 track 独占写文件（预计）

- `src/agent/tool_catalog/**`（新 surface_registry / meta_protocol_tools / surface_progress /
  surface_redaction + CMake 接线）
- `src/agent/mcp_server.{h,cpp}`（消费改造 + progress 通知 + 协商 + artifact_read + redaction 边界）
- `src/cli/cli_tool_commands.{h,cpp}`、`src/cli/cli_batch_runner.{h,cpp}`（新）、
  `src/cli/cli_commands.cpp`（dispatch append）、`src/cli/CMakeLists.txt`（append）、
  `src/cli/main_cli.cpp`（仅当 tools/batch 需要 legacy 路由——预计不需要）
- `pi/**`（只加文档/注释级 parity 说明；mcp_bridge.ts 行为已满足，不改逻辑）
- `tests/test_surface_parity.cpp`、`tests/test_cli_batch_manifest.cpp`、
  `tests/test_surface_protocol.cpp`（或并入 test_mcp_server.cpp append）、
  `tests/test_surface_e2e.cpp`、`tests/surface_mcp_host_main.cpp`、`tests/CMakeLists.txt`（append）
- `docs/agents/**`（surface contract 文档）
- 共享 integration：`CHANGELOG.md`、`.gitignore`、`tests/CMakeLists.txt`（append-only，
  最后 commit）

## 冲突处理纪律

Phase commit 后 `git fetch origin && git rebase origin/master`；若 #1008/#1009 中途合并，
重新审计新 master 的 surface 代码（尤其 mcp_server.cpp 是否被第三方修改）后再继续；
禁止 ours/theirs 覆盖；业务冲突逐 hunk 手工合。
