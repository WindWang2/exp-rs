# PARALLEL_OWNERSHIP — cloud-data-fabric-11

事实基点：2026-09-16 启动审计（见 BASELINE.md）。规则：open PR 的 changed files 默认 read-only；
已合并 PR 以新 master 为事实源重新审计；共享 integration files 只做最小 append-only。

## 本 track primary write scope（审计后确认，与 prompt 一致）

- `src/geospatial/fabric/**`
- `src/geospatial/io/**fabric**`（实际文件位于 `src/geospatial/io/` 下无 fabric 命名文件 — fabric 的 operator 面在 `src/operators/io/io_fabric_operators.*`，属于 scope 内的 `src/operators/io/*cube*` 及 fabric operators）
- `src/operators/io/io_fabric_operators.*`、`src/operators/io/io_operators_init.cpp`（fabric 算子注册段）
- `src/cli/*data*`（`cli_commands.cpp` 的 commandData 区段 + 其引用的 fabric 头）
- `tests/*fabric*`（test_io_fabric_*）
- `docs/io/**`
- `src/geospatial/multidim/**`（WP D 的业务主体 — prompt scope 中 "GDAL多维数组" 的落点；与 D19 dataset store 无交集）
- `src/geospatial/identity/**`（WP A 统一 identity — 9.0 已建立的 authority，本 track 只做 additive 扩展）
- `src/geospatial/remote/range_cache.*`（WP B 的业务主体）

## Open PR 逐一处置

### PR #1009 `zcode/execution-runtime-convergence-11`（open，UNSTABLE）

changed files（58）：`.gitignore`、`.planning/execution-runtime-convergence-11/**`、`CHANGELOG.md`、
`data/help/diagnostics.json`、`docs/execution/**`、`src/agent/data_platform_tools.cpp`、
`src/operators/CMakeLists.txt`、`src/operators/framework/**`、`src/processing/framework/**`、
`src/runtime/**`（chunk/exec/observability/worker）、`src/workflow/pipeline_run_coordinator.cpp`、
`tests/CMakeLists.txt`、`tests/test_chunk_*.cpp`、`tests/test_execution_*.cpp`、
`tests/test_chunk_graph.cpp`、`tests/test_large_scale_execution_10.cpp`、`tests/test_worker_lease_11.cpp`、
`.goal-loop-ledger.md`。

与本 track 交集：
- **无业务文件交集**。概念相邻点：其 "unified chunk contract / planner / governor" 是 tile 执行层
  （src/runtime），本 track 的 chunk planner 是 GDAL multidim/EO cube 维度切片层（src/geospatial/fabric）。
  不消费其任何 API（未合并）；不复制其功能。
- 共享注册文件：`tests/CMakeLists.txt`（各自 append 各自测试）、`.gitignore`（append 各自 .planning 白名单三行）、
  `CHANGELOG.md`（append 各自小节）— append-only，冲突概率低；rebase 时按内容合并。
- 其 body 声明 `src/workflow/pipeline_run_coordinator.cpp` / `src/agent/data_platform_tools.cpp` /
  `tests/test_large_scale_execution_10.cpp` 在 master@a5b11b7f 上 MSVC 编译失败。这三个文件全部属于
  #1009 的变更集 — **本 track 不修改**（read-only）。本 track 的构建/测试目标选择必须绕开对
  sicnu_workflow/sicnu_agent 的不必要链接；若测试目标传递依赖导致必须修复，则采用最小 build-unblock
  并在 DECISIONS.md 记录（与 #1009 的同名修复在 rebase 时择一保留）。

### PR #1008 `zcode/radiometric-spectral-workbench`（open，DIRTY）

changed files（45）：spectral/analysis/app/core/processing-algorithms 域 + `tests/CMakeLists.txt` +
`src/agent/CMakeLists.txt` 等。与本 track 零业务交集；共享 tests/CMakeLists.txt append-only。

## Remote branches 无 PR 的

无（启动时仅上述两个 remote feature 分支 + 两个指向 master 的空 11.0 分支名）。

## Open issues dedupe

#1001–#1007 全部不属于 fabric/multidim/cache/mirror/CLI-data 域（逐条判定见 BASELINE.md）→
全部 OUT_OF_SCOPE 登记，不修不关。

## 本 track 对共享文件的写入策略

| 文件 | 写入内容 | 形态 |
|---|---|---|
| `.gitignore` | `.planning/cloud-data-fabric-11` 白名单 3 行 | 独立 integration commit，append-only |
| `CHANGELOG.md` | 本 track 小节 | 独立 integration commit，append-only |
| `tests/CMakeLists.txt` | 新测试注册 | append-only |
| `src/geospatial/CMakeLists.txt` | 新 fabric/multidim 源文件 | append-only |
| `src/operators/io/io_operators_init.cpp` | 新 fabric 算子注册 | 最小 append |
| `data/help/**`（若 CLI 帮助目录存在） | 新子命令帮助 | 最小 append |

## 兄弟 track 分支名（无提交，仅记录）

`origin/zcode/teaching-lab-platform-11`、`origin/zcode/scientific-workflow-compiler-11` @ master —
出现提交后按本文件规则 4 重新审计。
