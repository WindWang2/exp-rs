# R15-E 所有权与排他边界（按实时 master 重算）

> 任务书基于 #1512 仍 OPEN 的快照编制。实时核查后 #1512 已合并（merge `f7693fce1`），
> 因此「31 文件只读锁定」不再适用；本文件按**实时**状态重新划定边界。

## 1. 本轨可写（默认允许）

| 目录 | 说明 |
|---|---|
| `src/app/**` | 桌面层（含 `experiment_studio/`） |
| `src/gui/**` | QGIS GUI 适配层 |
| `src/ui/**` | .ui 表单 |
| `src/help/**` | 帮助系统 |
| `src/teaching/**`、`src/teaching_admin/**` | 教学与教务 |
| `src/lab/**`、`src/lab_pack/**` | 实验包 |
| `resources/styles*.qss` | 仅在运行时确认为 E 独占时调整 |
| 非 #1513 占用的测试文件 | `tests/CMakeLists.txt` 走单文件短租 |

## 2. 绝对只读（其他轨/PR）

| 目标 | 归属 | 理由 |
|---|---|---|
| `src/analysis/segmentation/rs_multires_segmenter.cpp`<br>`src/operators/rs/rs_sam_classify_operator.cpp`<br>`src/processing/algorithms/image_fusion.cpp`<br>`tests/test_image_fusion.cpp`<br>`tests/test_obia_segmentation.cpp`<br>`tests/test_spectral_classification.cpp` | **#1514（r15-A，OPEN）** | 算法轨生产文件 |
| `src/cli/cli_commands.cpp`、`src/data/**`、`src/dataset/**`、`src/experiment/**`、`src/geospatial/**`、`tests/test_r15_B_*`、`tests/test_cli_commands_json.cpp`、`tests/test_io_remote_validator.cpp` | **#1513（r15-B，OPEN Draft）** | 数据持久化轨 |
| `src/agent_ops/**`、`src/science_context/**` | D 轨 / 其他 PR | 显式禁止 |
| 算法实现 | 只调用、不改 | 任务书硬约束 |

## 3. #1512 已合并后的处理规则

#1512 的 31 个文件**已随 `f7693fce1` 进入 master**，不再是他人未落地的工作。处置：

1. 先完整读合并 diff 与其新增测试（`test_r14_D_durable_writes.cpp`、`test_r14_D_vector_edit_honesty.cpp`、`test_theme_selector_parity.cpp`、`test_i18n*`）。
2. 只在这些文件中修**合并后仍真实残留**的缺陷；每条必须有本轨的 RED→GREEN 证据。
3. 不做「暗中 cherry-pick」：本轨不引入 #1512 之外的其他分支提交。
4. 若某问题已由 #1512 修好，归入 `FIXED_ON_MASTER`，不重复写。

## 4. 短租文件（改动需登记 + 释放后立即 fetch 复核）

| 文件 | 计划改动 | 状态 |
|---|---|---|
| `tests/CMakeLists.txt` | 仅追加本轨新增测试目标/源文件行，不动 #1513 的块 | 未开始 |
| `.gitignore` | 不改动 | — |

## 5. 资源隔离

- 构建树：`exp-rs-r15-e-20261017/build-dev`（独立，不与其他 worktree 共用 CMakeCache / ninja 文件）
- 不挂 vcpkg toolchain（并行轨道持有 `/c/deps/vcpkg/buildtrees/vcpkg-running.lock`），改用 `CMAKE_PREFIX_PATH` 指向 `C:/deps/vcpkg/installed/x64-windows`
- 编译 `-j2` 上限；测试 `CTEST_PARALLEL_LEVEL=1`
- 不使用本机 `reg.exe`/`vcvars64.bat`（沙箱拦截），改用 `env_r15e.cmd` 显式设置 VS/SDK 环境变量
