# R15-C Ownership — 写入边界与排他声明

## Writable（生产代码，本轨 PR 的全部源码改动）

* `src/operators/runtime/**` — 模型推理 provider/tile engine/publish/provenance/tensor blob/detection stack
* `src/operators/framework/model_catalog.{h,cpp}` — 仅限模型注册/清单契约（清单 schema、identity、health、validate 表面）；同目录其他文件只在确有必要且单独 commit 时短租
* `src/runtime/chunk/**` — 分块流水线（C12）
* `src/runtime/observability/**` — 遥测/故障登记（只做与本轨失败路径相关的小修）
* `models/**` — 仅针对**现有** manifest 的修错（shape/dtype/契约矛盾），不新增模型条目

## Conditional（审查后可小范围维护，优先不改）

* `src/runtime/gpu/**` — #1505 已合并（锁定解除），但除有确证缺陷外不改；`gpu_plane.cpp` 内部 warm-eviction 逻辑不重做
* `src/runtime/exec/**`、`src/runtime/worker/**` — 仅在与模型执行直接相关的缺陷上动

## Read-only（他轨/他 PR 所有权；只读审计并记录）

* `src/agent/**`、`src/workflow/**`（D 轨）
* `src/processing/algorithms/**`、`src/operators/rs/**`、`src/operators/gdal/**`、`src/operators/otb/**`、`src/operators/io/**`（A/B 轨）
* `src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`（#1504/#1513 范围）
* `src/python/isolated/**`（#1505/#1486 范围）
* `src/app/**`、`src/ui/**`、`resources/translations/**`（#1512/D 轨）
* `src/sdk/**`、`src/cli/**` — 只读审计（C14 入口对照）；缺陷记录给对应 owner
* `.github/workflows/**`、`scripts/**` — 不改

## 短租约共享文件（单文件短租：登记→最小行改→释放→fetch 复查）

* `tests/CMakeLists.txt` — 已有 #1513（draft，尾部 9818+ 追加）与历史 R14-B 块（3374-3431 行附近）。本轨新测试注册插入**模型运行时测试块附近（错开 9818 尾部块）**，纯追加，不重排既有行。
* 根 `CMakeLists.txt`、`.gitignore` — 预期零改动；如需白名单新测试产物，单独 commit 并在此登记。

## 与 open PR 的去重边界

* **#1514 (r15-A):** 文件零重叠 → 无动作。
* **#1513 (r15-B draft):** 唯一共享文件 `tests/CMakeLists.txt`，其追加块在文件尾部；本轨错开插入。不 cherry-pick、不覆盖。
* **#1505 (merged):** 其 gpu_plane/python-pool 修复只做回归验证（编译+既有测试），不在本分支重写。
* **#1508 (merged, R14-B):** 已完成项不重做；其显式遗留项（model_catalog 注册期 square-head、性能实测）是本轨 C02/C15 的正当工作。

## 硬性纪律

* 不写 master、不 reset --hard、不 clean -fd、不动其他 worktree。
* 每次提交围绕一个可复现缺陷簇，小可撤回。
* 每轨独立构建树 `build-r15-c`；全局同时最多一个重型构建；编译 -j2 上限（内存吃紧降 -j1）；测试 -j1。
* 不伪造结果；未运行的硬件/平台测试明确标注 NOT VERIFIED。
