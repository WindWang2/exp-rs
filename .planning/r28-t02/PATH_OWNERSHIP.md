# PATH_OWNERSHIP — R28-T02 写入边界

## 本轨主写（R28-T02 任务包"所有权"节）

- `src/data/**`（存储/导入/身份/泄漏审计基础）
- `src/dataset/**`
- `src/geospatial/**`
- `src/runtime/**`
- `src/jobs/**`
- `src/processing/framework/**`
- `src/operators/runtime/**`、`src/operators/io/**`
- 模型 catalog/runtime 相关 framework 文件（`src/operators/framework/model_catalog.*` 等）与实际存在的推理 entry operators（`src/operators/rs/` 中 infer/segment 类，只做必要兼容修复）
- `models/**` 必要兼容修复及专属测试
- `src/platform/durable_sidecar.*` 等持久化底座：仅最小变更并通知其他消费者

## 只新增（唯一命名）

- `tests/test_r28_t02_*.cpp`
- `docs/development/r28/t02/**`

## 共享热点（最小追加原则）

- `src/cli/cli_commands.cpp`：`data preflight` 子命令 + cube/mirror 路径的 CacheGuard（R1 P2-1 处置）——纯加法分支（新 subcommand 分支 + 局部 helper + usage 字符串）；不改任何既有命令行为面（`data cube/mirror` 仅新增 --allow-private-network 消费与 cache 安装，行为面为"远程资产从裸读变受护栏读"，见 DECISIONS D-9）
- `tests/CMakeLists.txt`：只追加带 `r28-t02` 标记的最小注册块；不删他人 target
- 根 CMake / 公共构建体系：T05 主责；本轨仅独立构建所必需的最小机械接线（单独 commit + 接口说明）

## 禁改

- `src/app/**`（T04；含 stac_browser_dialog 的 /vsicurl/ 残留 → HANDOFF）
- `src/core/**` 整体重写（禁）
- `src/agent/**`、`src/python/**`、`src/workflow/**`、`src/teaching*/**`、`src/experiment/**`（T03/T05/T01 域）
- 算法数学/分类训练（T01）：`src/operators/rs/**` 的数值语义不改，只改推理调度/资源相关接线
- 用户工作区 `main`（任何操作）

## 跨轨接口约定

- 取消语义底座（JobEngine/ExecutionPlane）归本轨；workflow 编排（T05）与 UI 展示（T04）各自消费
- `geo::remote::safeRemoteRasterPath` 为唯一远程入口：T04 GUI 需换接（HANDOFF）
- 快照/生成器：本轨真实构建后用仓库生成器重建，不手填

## 已知同机并行

- R28-T01（worktree exp-rs-r28-t01-science-20261104T180334Z）：science 域。共享风险=tests/CMakeLists.txt 追加块交错 → 只追加不修改既有行。
- 用户本人 #1813（fix/ui-review-2026-11-05）：src/app UI + resources/translations + 3 个 UI 测试文件。零重叠。
