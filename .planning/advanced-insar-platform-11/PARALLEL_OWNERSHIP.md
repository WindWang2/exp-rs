# PARALLEL_OWNERSHIP — 并发 PR / branch 文件级归属与处理策略

审计时点：2026-09-15，`origin/master = a5b11b7f`。

| 并发对象 | 状态 | changed files 与本 track 交集 | 处理策略 |
|---|---|---|---|
| PR #1008 `zcode/radiometric-spectral-workbench` | open, CONFLICTING | 业务文件零交集。共享集成文件：`.gitignore`、`src/agent/CMakeLists.txt`、`src/analysis/CMakeLists.txt`、`src/app/CMakeLists.txt`、`src/core/CMakeLists.txt`、`tests/CMakeLists.txt` | 业务文件不受影响。共享集成文件本 track 只做 **append-only 最小行级追加**（.gitignore 白名单一行、tests/CMakeLists 新增 test target 块、processing/operators CMake 新增源行）。若 rebase 时冲突，保留双方追加行，不覆盖对方追加内容。不复制/不重做 spectral/radiometric 功能。 |
| PR #991 D18 Mission Workbench | **已合并**（c5d4aafe） | 已成 master 事实 | 无需处理；不触碰其 owned paths（workbench internals） |
| PR #992 D19 Dataset Foundry | **已合并**（1cea9892） | 已成 master 事实 | 同上 |
| Issues #1001–#1007 | open | io/workflow/dataset/georef/agent | 全部 OUT_OF_SCOPE（见 BASELINE.md dedupe 结论）；不修改他人 issue |

## 本地并发 worktree（origin 上尚无对应 open PR，2026-09-15 观测）

| 本地 worktree / branch | 自述范围 | 与本 track 交集 | 处理策略 |
|---|---|---|---|
| `exp-rs-cn-eo-product-physics-11` (`zcode/cn-eo-product-physics-11`) | CN 产品（GF-3 SAR/GF-4）**产品解码**：`src/geospatial/products/`、import 算子 | 其 GOAL 明确排除"SAR algorithm kernels outside product decode"；理论交集仅在共享注册文件 | 互不写对方业务文件；共享文件 append-only；rebase 时逐行裁决 |
| `exp-rs-execution-runtime-convergence-11` (`zcode/execution-runtime-convergence-11`) | 执行面/runtime 收敛 | 无 SAR 交集 | 同上 |
| `exp-rs-geoai-promptable-foundation-platform-11` (`zcode/geoai-...`) | GeoAI agent tools + HTTP surface（ADR-0198） | 无 SAR 交集 | 同上 |
| `exp-rs-spectral-intelligence-11` (`zcode/spectral-intelligence-11`) | spectral 智能化 | 无 SAR 交集；与 PR #1008 同域不同 track | 同上 |
| `exp-rs-temporal-intelligence-11` (`zcode/temporal-intelligence-11`) | 时序智能化 | 无 SAR 交集（sar_temporal 域不在其命名范围；若 rebase 时出现重叠逐案裁决） | 同上 |

## 本 track 主写域（启动审计后确认，未扩大）

- `src/processing/algorithms/sar/**`（新增模块文件；既有文件仅在明确必要时最小扩展）
- `src/operators/rs/rs_sar_*`（新增 4 个算子文件 + `rs_sar_unwrap_operator` 既有文件内扩展 provider seam）
- `src/processing/CMakeLists.txt`、`src/operators/CMakeLists.txt`（append-only 源行）
- `tests/CMakeLists.txt`（append-only test target）、`tests/test_sar_*.cpp`（新增）
- `data/agent/capabilities/sar.json`（append 算子条目）
- `data/processing/algorithm_meta/capability/rs-sar-*.json`（新增）
- `src/agent/harness/capability_catalog.cpp`（kFailureModeCodes append）
- `src/agent/harness/harness_error.cpp`（新错误码分类 append）
- `docs/processing/sar-domain.md`（append 章节）
- `pi/knowledge/capability-sar.md`（append，若存在且结构允许）
- `.gitignore`（append 一行白名单）
- `data/contracts/contract_graph.snap.json`（仅由官方 `contract_inventory` 工具再生）

## 共享文件修改纪律

1. CMake/注册/capability/contract 快照类修改推迟到每个 Phase 的独立 integration commit。
2. append-only；diff 中不出现对他人行的改写。
3. 每个 Phase commit 后 `git fetch origin && git rebase origin/master`；业务冲突时逐行
   人工裁决（禁止 ours/theirs 整体覆盖科学代码）。
