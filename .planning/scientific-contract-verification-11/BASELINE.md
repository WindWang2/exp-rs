# BASELINE — scientific-contract-verification-11

Phase 0 只读审计，2026-09-16，主仓库 `C:\Users\wangj.KEVIN\projects\exp-rs`。全部事实以本文件为准（prompt-generation snapshot 已过时）。

## Origin / branch facts

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`（"fix: fail-closed fixes for review issues #994–#999 (#1000)"）。**本 track worktree 即从此 SHA 创建。**
- Prompt snapshot 的 `ebcafb4d` 已过时；#991 (D18 Mission Workbench) 已合并为 `c5d4aafe`，#992 (D19 Dataset Foundry/Benchmark) 已合并为 `1cea9892`。
- 最近 master 提交（top 20 见启动命令输出）：a5b11b7f (#1000 fail-closed fixes)、1cea9892 (#992 merge)、c5d4aafe (#991 merge)、77e178ac (#993 CI fixes)、08264801/ec1a4d96/d3387fcb (D19 系列)、ebcafb4d (#990)。
- Remote branches（除 master/HEAD）：`origin/zcode/execution-runtime-convergence-11`、`origin/zcode/radiometric-spectral-workbench`。

## Open PRs（read-only，见 PARALLEL_OWNERSHIP.md）

| PR | branch | mergeState | 主题 | changed-file 摘要 |
|---|---|---|---|---|
| #1008 | `zcode/radiometric-spectral-workbench` | CONFLICTING | 辐射定标/6S 大气校正/光谱 workbench (Day13) | src/core/radiometric_state、spectral_library；src/analysis/atmospheric、hyperspectral；src/processing/algorithms/radiometric_calibration、spectral_indices、spectral_unmixing；src/app/widgets；src/agent/spatial_tools；tests/test_spectral_*、test_radiometric_* |
| #1009 | `zcode/execution-runtime-convergence-11` | MERGEABLE | 执行运行时收敛（chunk contract、crash-safe resume、governor、worker lease、telemetry） | src/runtime/chunk/**、src/runtime/exec、src/runtime/worker、src/runtime/observability、src/operators/framework/{chunked_run,rs_operator_error,rs_operator_context}、src/processing/framework/{fused_chain,local_worker_pool}、src/workflow/pipeline_run_coordinator.cpp、tests/test_chunk_*、test_execution_*、test_worker_lease_11 |

## Open issues（逐条 dedupe）

| Issue | 主题 | dedupe 结论 |
|---|---|---|
| #1001 | `io:clip` 把 srcCrsOverride 当 targetCrs 用（silent wrong clip，critical/P1） | 在 master 未修复（io:clip 实现未含 fail-closed 分支）。**OUT_OF_SCOPE**：修复点在 `src/operators/io`（本 track read-only）；记录入 EVIDENCE/FAILURE_MATRIX，作为 follow-up。 |
| #1002 | workflow registry node executor 不验证 artifact 文件存在（fail-open，P1） | 在 master 未修复。修复点 `src/workflow`（PR #1009 正在改 pipeline_run_coordinator.cpp，强冲突区）。**OUT_OF_SCOPE**，follow-up。 |
| #1003 | dataset joinFeaturesBySampleId 把 JSON-null 必需列当存在（P1） | 在 master 未修复。修复点 `src/agent/data_platform_tools.cpp`（#1009 也改此文件）。**OUT_OF_SCOPE**。 |
| #1004 | dataset:qa 在 scan_capped 时 identity 仍 Passes uniqueness（P1，#996 residual） | 在 master 未修复。同 #1003 区域。**OUT_OF_SCOPE**。 |
| #1005 | mapPickToLayerCrs 在 CRS transform 抛异常时返回未变换画布点（silent wrong GCP，critical/P1） | 在 master 未修复。georef/交互层，本 track read-only。**OUT_OF_SCOPE**。 |
| #1006 | PipelineRunCoordinator 仍软默认 syntheticExecute（P2，#999 residual） | #1000 已修 #994–#999，本条为其 residual，仍 open。workflow 区（#1009 冲突区）。**OUT_OF_SCOPE**。 |
| #1007 | dataset:qa 从不审计 CRS（silent CRS gap，P2） | 在 master 未修复。dataset 区。**OUT_OF_SCOPE**。 |

共同模式（记录为验证缺口佐证）：这 7 条全部是 **fail-open / 静默错误语义** 类缺陷 —— 正是本 track 的 failure-semantics contract + negative-lane 要系统性防的类别；但修复位置全部在他人 own 的实现文件，本 track 只做"契约+测试能暴露此类缺陷"的机制，不做点修。

## 旧 ISSUES.md（D3 backlog）核验

- T-1（temporal_monitor scenes）→ 已修（`rs:temporal_monitor` 接受 inline scenes，CHANGELOG 10.0）。
- T-2 → 已修（`rs:temporal_regularize`）。T-3 → 已修（`rs:temporal_harmonic_breaks`）。C-2 → 已修（`rs:temporal_extract_regions`）。
- S-1/S-2/H-1/H-2/H-3/C-1：属算子能力缺口（非契约缺口），无本 track 相关性，不实施。
- **不把 ISSUES.md 当 live backlog。**

## Verification 10.0 基线（subagent #1 审计结论，路径已核对）

1. `src/contracts/scientific_contract.h`（struct L36–90，闭式词表 L94–103）+ `.cpp`（family 工厂 L148–232，registry L235+，重复 id abort）。覆盖 **115 个唯一 `rs:` id**，由 `tests/test_scientific_contract_10.cpp` 对 live registry 双向强制。**io:(10)/otb:(4)/opencv:(6)/cartography:(5) 共 ~25 个一等算子无 scientific contract**（validateScientificContract 硬编码 rs: 前缀）。
2. determinism 双权威：schema stamp（`rs_schema.cpp` `stampDeterminismGrade` L38–47）+ capability sidecar `capability.determinism.grade`；`test_drift_projection_10` L112 绑定二者一致。**债务：`determinismGrade() const override` 全库仅 46 处；`rs_operator.h` 默认返回 "tolerance" → 未覆盖算子静默兜底；`determinism()` 默认 BitExact 仅 6 处 override；sidecar 125 个中 109 bit_exact / 6 tolerance / 其余无字段；`stochastic:true` 为 0。**
3. Ladder：`scripts/verification_ladder.py`（L0–L8 lane 硬编码，schema exp.verification.ladder.v1）。**L3–L7 证据缺口官方记录于 `.planning/scientific-contract-verification-10/EVIDENCE.md`（skipped=3, not_built=16）。**
4. Metamorphic 仅 `test_science_verification_10.cpp` 1 条（NDVI band-scale invariance）；known-answer 语料 14+6 条；**无独立高精度数值 oracle lane**。
5. Provenance：`exp-rs-prov/1`（模型推理 sidecar + `provenance_verify.h`）；`SICNU_*` canonical metadata；contract 侧 `kProvenanceExpectations`。
6. Contract graph：`src/contracts/contract_graph.{h,cpp}` + `graph_assembly.cpp`，快照 `data/contracts/contract_graph.snap.json`（238KB，字节比对 gate；10.0 时 nodes:973 edges:386 findings:0）。
7. 无 ctest LABELS；lane 划分在 verification_ladder.py。构建：CMakePresets `dev-default/ci-fast/...`；测试 Catch2，`sicnu_add_test` 固定链接集（qgis_core/qgis_gui/sicnu_data/processing/jobs/task_center/agent/operators/workflow_runtime）。

## 本机构建环境（Windows）

- MSVC 2022 Community（vcvars64）；Ninja `C:\Qt\Tools\Ninja\ninja.exe`；CMake 3.27.2 (VS)；Qt 6.8.0 msvc2022_64 + qca + kc at `C:\deps\`；vcpkg toolchain `C:\deps\vcpkg`；Catch2 本地源 `C:\deps\catch2-src`（FETCHCONTENT_SOURCE_DIR_CATCH2）。
- sibling worktrees（execution-runtime-convergence-11 等 5 个）已有完整 build-dev 树（17GB 级），证明本 configure 配方可构建：
  `cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_PREFIX_PATH="C:/deps/Qt/6.8.0/msvc2022_64;C:/deps/qca-install;C:/deps/kc-install" -DCMAKE_TOOLCHAIN_FILE=C:/deps/vcpkg/scripts/buildsystems/vcpkg.cmake -DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src`
- 负载测量：Git Bash 下无 load average；CPU/RSS 用 `tasklist`/`wmic`/PowerShell 抽样；load 不可测（记 EVIDENCE，保持 -j2 上限）。

## 本地并发 11.0 worktrees（未推送分支，同样 read-only）

advanced-insar-platform-11、cn-eo-product-physics-11、execution-runtime-convergence-11（=PR #1009）、geoai-promptable-foundation-platform-11、linked-visual-analytics-11、scientific-workflow-compiler-11、spectral-intelligence-11、teaching-lab-platform-11、temporal-intelligence-11。文件级 overlap 处理见 PARALLEL_OWNERSHIP.md。
