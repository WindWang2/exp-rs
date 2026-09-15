# EVIDENCE — 本地验证记录（Local evidence only; no online CI dependency）

格式：每条 = 日期/Phase | 命令 | exit code | 关键输出摘要。所有 claim 均可本地复现。

## Phase 0（2026-09-15）

| 命令 | exit | 摘要 |
|---|---|---|
| `git fetch origin --prune` | 0 | master 前移 007e70cf→a5b11b7f；新 remote 分支 zcode/radiometric-spectral-workbench |
| `git rev-parse origin/master` | 0 | `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` |
| `gh pr list --state open --limit 100` | 0 | 仅 #1008（radiometric-spectral-workbench，DIRTY） |
| `gh issue list --state open --limit 200` | 0 | #1001–#1007 共 7 条（R2 残留，均非 execution 域） |
| `gh pr view 1008 --json ...` | 0 | 41 files，与本 track scope 交集 0（BASELINE.md） |
| `git worktree add ../exp-rs-execution-runtime-convergence-11 -b zcode/execution-runtime-convergence-11 origin/master` | 0 | HEAD=a5b11b7f |
| 三路只读 Explore 审计（runtime / jobs+authority / operators seam） | 0 | BASELINE.md「代码结构审计结论」 |

## OUT_OF_SCOPE（范围外发现，不修）

- issue #1001 io:clip srcCrsOverride 误作 targetCrs（critical/P1）— src/operators/io，非本 track scope。
- issue #1002 workflow registry node executor fail-open（critical/P1）— src/workflow。
- issue #1003 dataset joinFeaturesBySampleId JSON-null（P1）— src/dataset。
- issue #1004 agent dataset:qa scan_capped uniqueness（P1）— src/agent。
- issue #1005 georef mapPickToLayerCrs（P1）— georeferencer。
- issue #1006 workflow PipelineRunCoordinator syntheticExecute 默认（P2）— src/workflow（注：代码中类名为 WorkflowRunCoordinator）。
- issue #1007 dataset:qa 不审计 CRS（P2）— src/dataset。
- 审计另见：ChunkedProcessor 注释声称 JobEngine clamp 2..4 线程与实际 cores−1 不符（预算注释过期，P3 记录不修——src/processing 域）。

## 资源监控说明

宿主 Windows + Git Bash：load average 不可直接测量（无 /proc/loadavg 语义）→ 按 GOAL 允许，记录一次 not-executed，build 一律 `-j2` 上限；RSS 用 `tasklist` 抽查记录于各 build 段落。

## Phase 1-7（2026-09-15，进行中）

| 命令/事件 | exit | 摘要 |
|---|---|---|
| `dev.cmd configure`（第 3 次，含 FETCHCONTENT_SOURCE_DIR_CATCH2） | 0 | Configuring done 163.2s + Generating 36.3s；build.ninja 生成 |
| `dev.cmd configure`（第 4 次，增量） | 0 | Generating 37.6s |
| `cmake --build build-dev -j 2 --target sicnu_runtime` | 0 | 9/9 编译+链接通过（tile_run_contract/resumable_tile_run/execution_governor/worker_lease 首版） |
| 独立 review Round 1（resume 协议轴） | — | P0=1（identity key `:` 分隔符 Win32 非法）P1=1（空 journal 头崩溃陷阱）→ 全部修复（REVIEW_LOG） |
| 独立 review Round 2（治理/lease/adoption/测试轴） | — | P1=2（planner 上界 2 tiles/stage、_getpid POSIX）P2=2 P3=7 → 全部修复（REVIEW_LOG） |
| 全量测试目标编译（后台，-j2） | 进行中 | qgis_core 阶段 ~885/3277 边 |

### 资源抽查（构建期）

- ninja ×2 ≈ 5MB each；cl.exe ×2 ≈ 186MB + 86MB；-j2 恒定（未超限，无需降 -j1）。
- load average：Git Bash 不可测量 → not-executed（PERFORMANCE.md 记录一次）。

### not-executed 清单

- 在线 CI：全部不等待不引用（GOAL ci=none）。
- POSIX lane 编译：本机 Windows；`_getpid` 等 ifdef 已由 reviewer 核对（Round 2 #2 修复）。
- Windows fsync 真实持久性（断电语义）：rename 为原子边界（D-016，沿用既有契约）。

## 测试执行结果（2026-09-15/16，本机 MSVC/Ninja，QT_QPA_PLATFORM=offscreen）

全部新套件串行执行（run-one.cmd，等价 ctest 语义）：

| 套件 | exit | 断言/用例 |
|---|---|---|
| test_execution_authority_11 | 0 | 17 / 4 PASS |
| test_chunk_contract_11 | 0 | 42 / 7 PASS |
| test_chunk_resume_11（含 REAL 子进程 _Exit(70) 崩溃） | 0 | 47 / 8 PASS |
| test_execution_governor_11 | 0 | 75 / 5 PASS |
| test_worker_lease_11 | 0 | 33 / 5 PASS |
| test_execution_telemetry_11 | 0 | 10 / 3 PASS |
| test_chunk_adoption_11 | 0 | 27 / 6 PASS |
| test_execution_scale_fault_11 | 0 | 86 / 4 PASS |
| test_chunk_graph（回归） | 0 | 354 / 30 PASS |

构建过程中的 master 既有破坏（OUT_OF_SCOPE，做最小 build-unblock 修复）：
- `src/workflow/pipeline_run_coordinator.cpp`（PR #991 产物）：Win32 分支缺 `<fcntl.h>`（_O_WRONLY/_O_BINARY），MSVC 编译失败 → 补 include（1 行）。
- `src/agent/data_platform_tools.cpp`（PR #992/D19 产物）：`BenchmarkService` 无限定使用且无 `using namespace sicnu::experiment;` → 补 using-directive（4 行含注释）。
两者均非本 track 业务代码；修复为纯 build-unblock。**master@a5b11b7f 在本机 MSVC 环境不能完整编译 agent/workflow 目标**（记录为观察事实）。

测试驱动的实现修复（本 track 内，随实现 commit）：
- ResumableTileRun 现创建 statePath 父目录（此前首个 appendCommit 因目录缺失失败）。
- identity key 分隔符 `-`（review R1-P0）后所有路径/断言复核通过。

## Phase 8 验证（2026-09-16）

| 套件/命令 | exit | 结果 |
|---|---|---|
| test_worker_host（池 lease 接线回归，需 sicnu_worker.exe） | 0 | 61 断言 / 13 cases PASS |
| test_fused_chain（取消桥回归） | 0 | 44 / 4 PASS |
| test_external_memory_10（写作用域修复后） | 0 | 50 / 8 PASS |
| test_large_scale_execution_10 | 0 | 6218 / 5 PASS |
| test_job_engine | 0 | 446 / 34 PASS |
| **opt-in 规模门** SICNU_SCALE_11=1（100k 真实 tile：37,000 提交后硬停→恢复，reuse≥37k，kernel<total+1000） | 0 | 90 断言 / 4 cases PASS；wall 14m57s（记录值，非门） |

测试驱动修复（已随实现 commit）：DiskTileStore::write(lease) 的 ofstream 现于 finalize 前关闭作用域——纯 STD 对照 repro 证明 MSVC 下打开中的文件 rename 必败（sharing violation），该缺陷在 master 上即存在且使 test_external_memory_10 在 Windows 必红。

## Drift gate 闭环（2026-09-16）

- `test_diagnostics_contract_9` 捕获本 track 两个新错误码缺 curated page → 已在 `data/help/diagnostics.json` 追加两条 operator 家族页面（+47 行 append-only），**operator 家族 0 残留**。
- 该套件其余 7 个失败全部是 harness 家族码（CATEGORICAL_MISMATCH / FACT_CONFLICT 等，来自 master 的 src/agent/harness/harness_error.cpp，D18/mission track 产物）——pre-existing，本 diff 零重叠（对照：本 diff 未新增任何 harness 码，agent 目录仅 1 个 build-unblock 文件）。
- `test_scientific_contract_10`（`rs:change` 缺合约记录）与 `test_help_coverage`（workbench.* help 缺失、rs_glossary id 空）失败：实体均在 master 上，来自 D15/D18/visual-cartography 合并；本 diff 对 src/contracts、rs_operators_init、help 数据零改动（`git diff --name-only | grep -c` = 0）。pre-existing 记录。

## 终态（2026-09-16）

- 分支 `zcode/execution-runtime-convergence-11` 已推送（不 force）；**PR #1009 已创建（OPEN, MERGEABLE, 未 merge）**；不等待线上 CI。
- HEAD = 5123444dca977b3be6e50c76a4c820cfb2205f87；origin/master = a5b11b7f（自基线未动，无需 rebase）。
- `git diff --check origin/master...HEAD` exit 0；冲突标记 0；diff 57+ 文件无二进制/生成物；工作树干净（仅被忽略的 .planning 非 md 本地日志）。
- Oracle 状态：1 ✅（架构网+行为）2 ✅（真实崩溃零重算+漂移拒绝）3 ✅（队列/scratch/遥测硬上限+故障测试）4 ✅（证据在案）5 ✅（#991/#992 已合并，本 diff 不触碰其他 open PR 文件）6 ✅（diff check/标记/secret/生成物）7 ✅（14 套件连续两遍原样全绿）8 ✅（两轮独立 review，P0=P1=0，PR 已创建未 merge）。
