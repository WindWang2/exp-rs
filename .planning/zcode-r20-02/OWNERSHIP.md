# R20-02 OWNERSHIP — 文件级写权证明

基线 SHA：28e1891390bb812dc7ef2fe045ac90492c9525f1（= origin/master）。每次 commit 前用
`git diff --name-only 28e1891390...HEAD` 核对下表；出现表外路径必须停手复核。

## 计划写入清单（全部在 §3 优先允许域）
| 路径 | Owner | 依据 |
|---|---|---|
| src/operators/runtime/tile_inference_engine.cpp | 本轨 | src/operators/runtime/** 允许 |
| src/operators/framework/model_catalog.cpp | 本轨 | 确有该文件（model_catalog.*） |
| src/data/governance/workspace_lifecycle.cpp | 本轨 | src/data/** 允许 |
| src/data/artifact_object_pool.cpp | 本轨 | src/data/** 允许 |
| tests/test_model_runtime.cpp | 本轨 | test_*model* 允许（追加 TEST_CASE） |
| tests/test_model_catalog_v2.cpp | 本轨 | test_*model* 允许（追加） |
| tests/test_workspace_services.cpp | 本轨 | data/workspace 域测试（既有注册文件，追加） |
| tests/test_fault_injection.cpp | 本轨 | data/artifact pool 域测试（既有注册文件，追加） |
| .planning/zcode-r20-02/** | 本轨 | 轨账本 |

**零共享文件修改**：所有测试均追加进已注册的既有测试文件，不改 tests/CMakeLists.txt、根 CMakeLists、.gitignore。

## 只读域遵守
- src/operators/rs/**（#1544 等发现 → CROSS_TRACK）、src/analysis、src/processing、src/app、src/cli、src/python 顶层（#1647-1）、src/teaching、pi/、src/gui、src/core 全程未动。
- 主仓 /home/kevin/projects/rs-studio/main 与其他 worktree 零写入（误建 build-r20-02 已于 00:05 删除，主仓 status clean 实证）。

## 冲突监测
- push 前 `git fetch --prune origin` + 复查 open PR changed-files（当前 0 open PR）。
