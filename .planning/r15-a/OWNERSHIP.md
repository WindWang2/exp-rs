# R15-A OWNERSHIP — 所有权与排他边界

## 可写范围（本轨独占）

| 范围 | 说明 |
|---|---|
| `src/analysis/**` | 分类/分割/地理配准/大气/高光谱算法（含局部头文件修改） |
| `src/processing/algorithms/**` | 基础处理算子库（含局部头文件修改） |
| `src/operators/rs/**` | 遥感算子。**例外**：`rs_spectral_index_operator.cpp` 的永久避让锁已因 #1509 合并（2026-10-16）解除，但**必须先完整阅读 #1509 对该文件的 +4/-4 diff 并重新审计后才可写入**（计划在 ORACLES O-audit 中执行） |
| `tests/test_r15_A_*.cpp` | 本轨独立新测试文件 |
| 既有测试文件 | 仅追加本轨回归用例（如 `tests/test_image_fusion.cpp`、`tests/test_spectral_classification.cpp` 等），不改他人既有断言 |
| `.planning/r15-a/*.md` | 本轨规划/证据文档（`.gitignore` 已加 3 行白名单例外，遵循仓库约定） |
| `.gitignore` | 仅上述 3 行例外（已落地） |
| 本轨 build 树 | `build-r15-a/`（独立 CMakeCache/ninja/生成目录） |

## 只读范围（禁止抢改）

| 范围 | 归属 | 依据 |
|---|---|---|
| `src/core/**`（含 `temporal_cube.cpp`） | 核心层；#1505 已合并其修改 | 快照避让 + 已合并，仍不写（非本轨所有权） |
| `src/operators/runtime/**` | C 轨 | Prompt 边界 |
| `src/geospatial/**` | B 轨 | Prompt 边界 |
| `src/app/**` | E 轨 | Prompt 边界 |
| `src/agent/**`、`src/workflow/**` | D 轨 | Prompt 边界 |
| `src/python/**`、`src/science_context/**`、`src/verification/**`、`src/agent_ops/**` | 其他轨（#1505/#1509 刚动过） | 非本轨可写清单 |
| `tests/CMakeLists.txt`、根 `CMakeLists.txt` | 共享 | 仅单文件短租：登记计划→改少量独立行→释放；最终先 fetch master 查冲突 |
| 主 checkout 与 R10 worktree | 他人工作区 | 禁 reset --hard / clean -fd / 覆盖 |

## 资源纪律（跨轨共享机器）

1. 任一时刻全机最多一个重型构建（本机无 flock，以构建日志时间戳 + 单轨串行执行等价保证；启动构建前先探测）。
2. 编译严格 `-j2`（`CMAKE_BUILD_PARALLEL_LEVEL=2`）；RSS 异常则降 `-j1`。
3. 测试 `CTEST_PARALLEL_LEVEL=1`；GUI 相关设 `QT_QPA_PLATFORM=offscreen`。
4. CCACHE/临时目录/Qt 缓存/日志/测试产物全部隔离在本轨 worktree 内；`rm` 前确认目录属于本轨。
5. 不制造/下载巨量遥感数据：小数据 + 有界合成数据（复用 `tests/synthetic_raster_builder.h`、`synthetic_terrain_dem.h` 等既有设施）。
6. 不用 token 浪费代替验收：不为凑量重复构建/读写。

## 执行模式记录（诚实声明）

- 宿主（WorkBuddy）**未提供 `/teamwork-preview` 与 `/goal-loop` 指令**，也未安装 `.agents/skills` 中的 goal-loop 技能。按 Prompt 第 0 节规定，**记录"未安装/等价执行"事实**：采用 Prompt 内等价 Oracle-Ledger 规则（ORACLES.md + LEDGER.md），以 WorkBuddy 子代理（Agent 工具）承担实现者/测试者/独立 Reviewer 角色分工；独立 Reviewer 由**未编写本轨生产修改代码**的 subagent 担任，两轮审查。
