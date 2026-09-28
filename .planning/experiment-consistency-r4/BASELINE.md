# R4 Experiment Consistency — BASELINE (Phase 0 实测)

实测时间：2026-09-27（本轨道开工）；全部计数在 worktree `hardening/r4-experiment-consistency` @ 下述 SHA 上用 rg/stat 实测。

## 1. git 基线

- `origin/master` 实测 SHA：**`15e5c66b543ef3874cb929f17529ef456bd6c059`**（与提示词写作值一致 = PR #1333 合并点）
- 本地 master 落后 origin/master 159 提交（`git rev-list --count master..origin/master`），与本轨道无关（所有工作基于 origin/master 新 worktree）。
- worktree：`/home/kevin/projects/rs-studio/exp-rs-experiment-consistency-r4`，分支 `hardening/r4-experiment-consistency`。
- open issues：**0**（`gh issue list --state open` 实测）。

## 2. 在途 PR 盘点与 file-overlap map（gh pr list 实测 2026-09-27）

| PR | 分支 | 标题要点 | 与本白名单重叠 |
|---|---|---|---|
| #1340 | hardening/r4-operator-oracles | 算子正确性审计 7 修复 + 35 oracle | `tests/CMakeLists.txt`（仅此） |
| #1339 | hardening/r4-i18n-help | tr() 覆盖 29 文件、81 词表 | `tests/CMakeLists.txt` |
| #1338 | hardening/closure-io-processing-r4 | io/processing 原子发布、GDAL writer | 无（tests 均非本轨载体） |
| #1337 | hardening/closure-workflow-contracts-r4 | workflow/agent 语义收敛 | 无 |
| #1336 | hardening/closure-ui-runtime-r4 | i18n/help/lab-pack | 无 |
| #1335 | fix/review-p0-build-restore | master 构建/CI 修复 | `tests/CMakeLists.txt` |
| #1334 | fix/review-p1-security | MCP/CLI sandbox、PluginHost | `tests/CMakeLists.txt` |

**结论**：`src/experiment*`、`src/study*`、`src/scientific_state*`、`src/dataset/dataset_store_splits.cpp` 与全部在途 PR **零重叠**。唯一共享文件 `tests/CMakeLists.txt`（4 个 PR 在改）：本轨道只做"追加新测试目标注册"这一种改动，冲突面最小；PR 正文声明该重叠与 rebase 策略。

## 3. 锚定表复核（提示词值 → 实测值，漂移以实测为准）

| 锚点 | 提示词值 | 实测值 |
|---|---|---|
| `experiment_store.cpp` 体积 | 109,431 B | **106,993 B** |
| `src/experiment/*.h` 头文件 | 13 核心 | **21 个**（13 核心全在，另有 benchmark_compare/definition/runner/service、comparison_ext、experiment_ids、experiment_types、run_bridge） |
| `kMaxMatrixCells` | h:28 | **experiment_matrix.h:31**（=1000） |
| `runsForCell` | h:105/.cpp:304 | **h:105 / .cpp:304**（`qint64 limit = kMaxMatrixCells`）✓ |
| `outgoingEdges` | h:172/.cpp:2001 | **h:172 / .cpp:2001** ✓ |
| `flattenMetrics` | comparison_ext.cpp:175 | **:175（递归 :221，入口 :250-251）** ✓ |
| 50-twin 上限 | repeat_execution.cpp:136 | **:136 `/*limit=*/50`** ✓ |
| `candidatePaths` toInt | evidence_source.cpp:493/:506 | **:493 / :506** ✓ |
| `splitManifestsForVersion` | dataset_store.h:262/splits.cpp:132 | **h:262 / :132** ✓ |
| lineage 截断 | "100k 边" | **experiment_store.h:177 `allLineageEdges( qint64 limit = 100000 )`**；lineage.h:85/88 `ancestors`/`descendants` `maxNodes = 1000`；debugger/first_divergence.cpp:146 guard 100000 |
| seed ≥2^63 | r2 遗留 | **reproduction_bundle_import.cpp:197-216**（无 `seed_hex` 时 `toDouble(0)` 强转 `quint64`）；导出端 :216-220 已写 `seed_hex`（#1326） |
| benchmark 重复行 | 待契约决策 | **benchmark_runner.cpp:303-350**（`truthById.insert` last-wins :307；prediction 循环每行 `matrix.increment` :348） |
| markdown ≥4 反引号 | 残余向量 | **experiment/bridge/lab_report_writers.cpp:200-265** 用 ```` ```` ```` 四反引号 json 围栏 |
| `runById`/`promotionById` | fail-open | **store.cpp:996 / :2113**；`loadRunLocked` :549 解析失败返 nullopt；同文件已有修复先例：`ExistingRun` tri-state（:568-592）+ `promotionsForModel` fail-closed 注释 |
| ⑫ capsule 模块 | — | **src/experiment/capsule/**（document/io/readiness/diff/portability，9 文件） |
| 既有测试载体 | 待实测 | **13 个**：test_experiment_{capsule,debugger,evaluation,studio_core,studio_dock}.cpp + test_study_{analysis,e2e,exemplars,export,runner,sampling,spatial,spec}.cpp；scientific_state 族 test_scientific_*.cpp 10+（Phase 4 边界用） |

## 4. 12 条清单现状复验（逐条，详见 R4_BACKLOG.md）

全部 12 条在 `15e5c66b5` 上复验**仍然存在**（未被后续合并修掉；#1333 之后 master 无新合并）。逐条证据行号见 `R4_BACKLOG.md`。

## 5. 构建与测试基线

- 构建配方（复刻 r3 成功配置，规避 raise-compiler-stack.sh 吞错陷阱——直接 cmake，无包装脚本）：
  `cmake -B build-r4 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/g++-15 -DCMAKE_C_COMPILER=/usr/bin/gcc-15 -DENABLE_TESTS=ON -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`
- 资源红线：`ninja -j2`（nproc=16、RAM 62G，仍守 -j2）；`CTEST_PARALLEL_LEVEL=1`；GUI 测试 `QT_QPA_PLATFORM=offscreen`。
- 基线红绿分布：见下节（首次全量构建完成后回填）。

### 5.1 基线 ctest `-R "experiment|capsule|debugger|study|scientific_state" -j1`

（回填：PASS/FAIL 计数与失败清单）

## 6. 本轨道边界声明

- 允许触碰：`src/experiment/`（含 capsule/、debugger/、bridge/ 子目录）、`src/experiment_studio/`、`src/study/`、`src/scientific_state/`、`src/dataset/dataset_store_splits.cpp`（仅第⑩条最小修复）、`tests/test_experiment*`、`tests/test_*capsule*`、`tests/test_*debugger*`、`tests/test_study*`、`tests/CMakeLists.txt`（仅追加注册）、`.planning/experiment-consistency-r4/`。
- 白名单外一律不改；发现必须改的跨边界缺陷 → 记账移交，不顺手修。

## 7. 续作附录（2026-09-28 会话：merge origin/master a726d17a6）

- `origin/master` 实测前进至 **`a726d17a6224632d929e782e996351732632f272`**（较本轨基线 15e5c66b5 领先 **425** 提交；期间 #1334/#1335 已并入，多条并行轨道 R4 PR 落地）。
- **白名单重叠实测**：`git diff --name-only 15e5c66b5..a726d17a6` 与本轨 diff 文件全集（merge 首父时点 41 个）取交集 = 仅 `tests/CMakeLists.txt` 与 `.goal-loop-ledger.md`（共享账本）；`--no-merges` 提交里触碰 `src/experiment*|src/study*|src/scientific_state*|splits.cpp` 的计数 = **0** —— 12 条处置不可能被上游顺手修掉（merge 后已逐条 rg 复验在场）。
- **上游自带缺陷（已修复并记账）**：origin/master 的 `tests/CMakeLists.txt:14728` 残留孤立 `=======` 冲突标记（union 合并解法保留双侧但漏删分隔线；与上游自修过的 8781892972/6dff24166f 同一事故类），导致 **master 当前任何 configure 都在 tests/CMakeLists.txt 解析失败**。本轨 merge 提交后以 53658dba92 删除该行（两侧块均完整、两侧 .cpp 均在树）。
- **门禁架构修订**：终门禁改在**全新 `build-r4-gate` 目录**执行（复刻 light 选项 gcc-15/Debug/Ninja/ENABLE_TESTS=ON，无包装脚本）。测试注册为 `catch_discover_tests(DISCOVERY_MODE PRE_TEST)`——未构建可执行在 ctest 侧显示 `*_NOT_BUILT` 占位，构建完成即真实发现；门禁载体集 = 28 个可执行目标（experiment 族 9 + scientific_state 族 10 + study 族 8 + test_studio_live_e2e 作为受影响面附加证据）。`-j2` 全程。
- 基线红绿分布（§5.1）改在 build-r4-gate 全量门禁首跑时一并产出：PASS/FAIL 与失败清单区分"master 既有红"与本轨引入（对照手段：失败用例先在未含本轨改动的构建上复跑定性）。
