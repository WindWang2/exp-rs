# R22-03 Ownership

## 本轨独占写入（worktree agent/r22-inference-20261025T132554Z）

- `src/operators/runtime/tile_inference_engine.cpp`（FeatherAccumulator 及其调用点语义）
- `tests/test_r22_inference_feather.cpp`（新建）
- `tests/CMakeLists.txt`（仅一处精确 append：新测试目标注册块）
- `.planning/r22-inference/**`

## 只读（其他轨道归属）

- `src/data/**`（02 轨）、`src/processing/algorithms/**`（01 轨）、`src/app/**`（05/06 轨）——本轨零写入。
- `src/runtime/gpu/gpu_plane.cpp`、`src/runtime/chunk/chunk_graph.cpp`：#1554/#1553 已由 d9f1b886f1 修复，
  本轨不写（跨轨接口需求如出现记 CROSS_TRACK_FINDINGS.md）。

## 跨轨注册表（append-only 白名单）

- `tests/CMakeLists.txt`：唯一允许的跨轨文件；改动为单一注册块插入，已 `git log -p`/PR 文件列表核对无冲突占位。

## Git 安全承诺

- 不 `git add -A`；只 `git add <上述路径>`。
- 不使用 stash；不 `reset --hard`/`clean -fdx`；不动他人分支/worktree/PR/issue。
- 不 push master；只 push 本轨分支 `agent/r22-inference-20261025T132554Z`。
