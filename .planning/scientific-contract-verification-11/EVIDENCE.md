# EVIDENCE — scientific-contract-verification-11

所有 capability claim 必须对应本地命令 + exit code。Baseline `a5b11b7f`（2026-09-16）。本机：Windows 10.0.26200 x64，MSVC 14.38，Ninja `-j2` 硬上限，Qt 6.8.0，vcpkg。

## Phase 0（审计）

- `git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`；`gh pr list` → #1008 (CONFLICTING)、#1009 (MERGEABLE)；`gh issue list` → #1001–#1007。逐条 dedupe 见 BASELINE.md。
- Subagent #1（只读 Explore）完成 verification/contract 体系审计（115 rs: 全覆盖、~80+ determinism 未发布债务、metamorphic 1 条、L3–L7 skipped=3/not_built=16 官方记录）。
- Worktree `../exp-rs-scientific-contract-verification-11` 自 `a5b11b7f` 创建。
- Configure：`cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_PREFIX_PATH="C:/deps/Qt/6.8.0/msvc2022_64;C:/deps/qca-install;C:/deps/kc-install" -DCMAKE_TOOLCHAIN_FILE=C:/deps/vcpkg/scripts/buildsystems/vcpkg.cmake -DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src` → exit 0（988s + 76s）。

## Build（全量 + 增量历史）

- 全库栈首次构建（含 vendored qgis_core，3307 targets，-j2）→ 中途被 **master 预存 MSVC 编译缺陷**阻塞两次（见 Host P0 节）；修复后全部目标构建成功（build8 exit 0，后续增量均 exit 0）。
- 构建 RSS 抽样（60s 周期，PowerShell Get-Process）：1.3–2.4 GB / 17–24 进程，未触发降 `-j1` 阈值。monitor.log 在 build-dev/（本地）。
- load average 不可测（Git Bash/Windows）→ 按 GOAL 记 not-executed，`-j2` 恒定上限。

## Host P0（master 预存，阻塞整个本地验证平台，已最小修复）

| # | 文件 | 缺陷 | 修复 | 归属证据 |
|---|---|---|---|---|
| P0-1 | `src/workflow/pipeline_run_coordinator.cpp` | `_O_WRONLY/_O_BINARY` 在 Q_OS_WIN 分支使用但 `<fcntl.h>` 只在 `!Q_OS_WIN` 分支 include（c5d4aafe/#991 引入） | `<fcntl.h>` 移入 Q_OS_WIN 分支（1 行） | `git diff origin/master` 未触该文件；PR #1009 亦改此文件（rebase 平凡） |
| P0-2 | `src/agent/data_platform_tools.cpp` | 裸用 `BenchmarkService`（sicnu::experiment）而无 using/限定（D19/#992 引入） | 1 条 using-declaration | 同上 |

## Gate 结果（worktree HEAD，全部本地直接运行；`run_test.cmd` = vcvars + DLL/PROJ_DATA 环境，-j1）

| Suite | Result |
|---|---|
| test_scientific_contract_10 | All tests passed (1480 assertions in 5 test cases) RUN_EXIT:0 |
| test_contract_census_11 | All tests passed (768 assertions in 7 test cases) RUN_EXIT:0 |
| test_science_verification_10 | All tests passed (1187 assertions in 5 test cases) RUN_EXIT:0 |
| test_verification_metamorphic_11 | All tests passed (1122 assertions in 6 test cases) RUN_EXIT:0 |
| test_contract_cross_surface_11 | All tests passed (361 assertions in 4 test cases) RUN_EXIT:0 |
| test_verification_numeric_reference_11 | All tests passed (234 assertions in 5 test cases) RUN_EXIT:0 |
| test_contract_determinism_11 | All tests passed (138 assertions in 4 test cases) RUN_EXIT:0 |
| test_mutation_kill_11 | All tests passed (42 assertions in 3 test cases) RUN_EXIT:0 |
| test_verification_failure_11 | All tests passed (32 assertions in 5 test cases) RUN_EXIT:0 |
| test_known_answer_corpus | All tests passed (105 assertions in 14 test cases) RUN_EXIT:0 |

## Ladder（capability-aware L0–L2，`verification_ladder.py --build-dir build-dev --lanes L0,L1,L2 --build-jobs 2`）

- **L0 ok / L1 ok**；L2 = 15 passed + 5 non-passing，JSON 报告：`.planning/scientific-contract-verification-11/ladder_l0_l2_run1.json`。
- 5 个非通过项的逐一分类（**全部与本 diff 无交集**，`git diff origin/master --name-only` 不含下列事实的 owning 文件）：
  1. `drift_projection_10`：rs:spectral_unmixing 等的 sidecar 缺 schema 参数行 —— hyperspectral-10 track 的数据-代码漂移（sidecar/schema 文件非本 track 触碰）。
  2. `contract_platform_9`：3 × duplicate_node（rs:gaofen/hj/zy3 capability 条目在 `data/agent/capabilities/preprocess.json` 两次 + `io.json` 一次）—— master 数据重复（两个 track 各 append 一次）。快照字节比较部分通过。
  3. `command_contract_9`：`cartography.repair` 等 command 无 help 页 —— cartography/workbench track 遗留。
  4. `diagnostics_contract_9`：harness 码 `CATEGORICAL_MISMATCH` 无 curated 页 —— 同类遗留。
  5. `contract_projection_9`：`rs:matched_filter` unresolved —— hyperspectral track 遗留。
  6. `fuzz_ipc`：420s/900s 超时 —— Windows named-pipe 仿真上的性能特征（standalone 探测进行中；如仍超时 → host limitation 分类）。

## Regenerated artifacts（conscious-diff 仪式）

- `data/contracts/determinism_census.snap.json`（新增）：166 entries。
- `data/contracts/contract_graph.snap.json`：990→**1075 nodes**、396→**441 edges**（151 条 scientific-contract 记录、phase/displacement 词汇）；3 个 pre-existing duplicate findings 原样保留（本 track 不修他人数据）。

## OUT_OF_SCOPE（记录不修）

- open issues #1001（critical/P1，io:clip CRS 混淆）、#1005（critical/P1，mapPick 未变换点）、#1002/#1003/#1004/#1006/#1007 —— 全部为 fail-open/静默语义类，修复点在他人 own 的实现区（io/workflow/dataset/georef/agent）。机制性防御属本 track（failure lane 的 F1–F5 正是此类缺陷的通用暴露器）。
- master 预存测试失败 5 项（见 Ladder 分类）+ fuzz_ipc 本机超时。
- `data/agent/capabilities/tools.json` 混入 13 个非算子的 cartography agent-tool id —— 已在 cross-surface gate 中按精确清单 allowlist + 计数约束（增长会失败），修复属 agent-knowledge track。

## Phase 预算注记

Phase 2–3 实际消耗超过规划包线 1.5×，原因：两个 master 预存 MSVC 编译缺陷（P0-1/P0-2）使本地验证平台完全阻塞，修复+全量重建（-j2 下 ~4h）消耗了大量轮次；GOAL 规定记录后继续。
