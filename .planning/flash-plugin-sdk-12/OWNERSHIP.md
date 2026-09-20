# OWNERSHIP.md — flash-plugin-sdk-12

## 可写（主 owner）

- `src/sdk/exprs/**`（plugin SDK 核心：version、manifest、capabilities、permissions、quotas、registry、loader、validator、package、diagnostics、ipc_*）
- `src/plugins/**`（framework/host/processing/layer_tree）
- `docs/plugins/**`、`docs/sdk/**`（插件文档）
- `tests/test_plugin_*.cpp`、`tests/test_exprs_plugin_*.cpp`、`tests/test_exprs_workflow_schema.cpp` 等本 Track 相关测试
- `.planning/flash-plugin-sdk-12/**`（本 Track 规划；需在 .gitignore 加白名单，见 DECISIONS.md）

## 只读

- `src/app/**`、`src/core/**`、`src/operators/**`、`src/processing/**`、`src/geospatial/**`、`src/workflow/**`、`src/data/**`、`src/agent/**`、`src/pi/**`、`src/experiment/**`、`src/gpu/**`
- `CMakeLists.txt` 顶层、`cmake/**` 公共模块：仅当新增 target 需要时做**最小 append-only** 接入，不改他人语义
- QGIS/OTB/ITK 外部依赖

## 共享 append-only

- `.gitignore`（追加本 Track 白名单条目；与既有 track 白名单同模式）
- `src/sdk/CMakeLists.txt`、`src/plugins/CMakeLists.txt`（新增源文件时 append）
- `src/sdk/exprs/plugin_diagnostics.h`（E6xxx/P codes 仅 append，不改既有值）
- `src/sdk/exprs/plugin_manifest.h`（manifest v1 字段仅 append optional）
- `src/sdk/exprs/plugin_permissions.h`、`plugin_capabilities.h`（枚举仅 append）

## 冲突应对

若发现并行 PR 修改共享文件：先 rebase 最新 origin/master，做最小集成，不复制他人实现。
