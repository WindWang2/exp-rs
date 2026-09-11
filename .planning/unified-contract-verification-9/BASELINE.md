# BASELINE — Unified Contract / Help / Verification / Portability / Release Platform 9.0

> Captured 2026-09-12 against `origin/master` = `f316dfdbb4`
> ("fix(issues): resolve all 30 P1/P2 issues (#853-#882)").

## 1. Repository / branch state

- Local `master` == `origin/master` == `f316dfdbb4` at capture time. Local
  master is treated read-only; all work happens in worktree
  `/home/kevin/projects/rs-studio/wt-contract-verification-9` on branch
  `feat/unified-contract-verification-9`.
- Recent merged PRs (latest first): #847 geospatial-data-fabric-8, #846
  scientific-processing-8, #845 professional-workbench-8, #844
  plugin-platform-8, #843 dataset-experiment-mlops-8, #842
  spatial-scientist-harness-8, #841 execution-plane-8, #840
  cartography-platform-8, #839 verification-platform-8, #837 model-runtime-8,
  then the 7.0/6.0 series. All 8.0 capability tracks are merged into master.
- After the 8.0 wave, two fix commits landed directly on master:
  `8f6293bceb` (P0 defects #848-#852) and `f316dfdbb4` (P1/P2 issues
  #853-#882). **Zero open issues** at capture time.

## 2. Open PRs (parallel -9 tracks — do not collide)

| PR  | Branch                              | Directory footprint (parents) |
|-----|-------------------------------------|-------------------------------|
| #883 | feat/scientific-algorithms-9      | src/operators/rs, src/processing/{algorithms,contracts}, src/experiment, src/geospatial/metadata, tests |
| #884 | feat/model-runtime-multimodal-9   | src/operators/{framework,rs}, src/experiment, docs/{adr,models,inference}, benchmarks |
| #885 | feat/spatial-scientist-harness-9  | src/agent/{harness,contracts,mapspec,spatial_tools}, data/agent/{capabilities,evals}, docs/adr |
| #886 | feat/scientific-mlops-9           | src/cli, src/dataset, src/experiment, tests |
| #887 | feat/geospatial-data-fabric-9     | src/geospatial/* |

No open PR exists for `feat/unified-contract-verification-9` — this track.

## 3. Remote branches

All `feat/*-5` … `feat/*-8` branches correspond to already-merged PRs
(historical residue, no independent commits ahead of master that matter for
this direction). The live `-9` branches are exactly the five open PRs above.
`fix/ci-*` branches correspond to merged CI-fix PRs (#833-#838).
Conclusion: "remote branch exists" ≠ "in development"; only the five `-9`
branches are active.

## 4. Direct predecessor: Verification Platform 8.0 (PR #839)

Already in master (reused, not rebuilt):

- `scripts/verification_ladder.py` — L0..L8 lanes, resumable, JSON results,
  honest `not-built/skipped` statuses.
- `scripts/collect_readiness.py` — machine+human readiness report.
- `src/geospatial/util/gdal_compat.h` — GDAL version seam.
- Trace adapters (WorkflowRunCoordinator, TaskCenter, OutputCommitter,
  DatasetStore, ExperimentStore) + `SICNU_FAULT_POINT` sites.
- Tests: `test_portability_contract`, `test_known_answer_corpus_8`,
  `test_trace_chain_8`, `test_contract_fuzz_ipc`, `test_contract_fuzz_ops`,
  `benchmark_scale8`, L0 header probes, fault8 case in
  `test_model_failure_matrix`.

Also already in master (from earlier waves) and checked for overlap:

- `test_help_coverage.cpp` — operator help coverage, parameter-descriptor vs
  schema consistency, command-id source-scan of `command_defs.cpp`,
  HarnessError/RSOperatorError → diagnostics drift check, secret scan.
- `test_capability_drift.cpp` — capability knowledge entries vs live
  operator registry, closed intent vocabulary, modality agreement.
- `test_algorithm_meta_drift.cpp` — algorithm_meta sidecars vs in-code
  descriptors (byte-compare, exact membership, **brittle `== 29` count pin**).
- `test_shortcut_conflicts.cpp` — QKeySequence source-scan conflict guard.
- `test_cli_commands_json.cpp`, `test_command_registry.cpp`,
  `test_knowledge_drift.cpp`, `test_spectral_formula_drift.cpp`,
  `test_contract_fuzz_{agent,data,io,ipc,lang,ops}.cpp`,
  `scripts/check_theme_parity.py`.

## 5. The gap this track closes (evidence)

The 8.0-era guards verify **schema ↔ help**, **knowledge ↔ registry**, and
**sidecar ↔ descriptor**, but nothing mechanically ties
**implementation-accepted parameters ↔ schema()**. All three operator-schema
drift issues of the #853-#882 batch were exactly that class and were fixed
*by hand* in `f316dfdbb4` with **no guard that would have caught them**:

- #872 `rs:infer`: implementation reads `params["device"]` (rs_inference_operator.cpp:468)
  but `schema()` did not declare it.
- #879 fusion aliases: implementation consumes `msWeights` for the linear
  method; schema omitted it.
- #880 `io:inspect`: implementation reads `includeStatistics`; schema omitted it.

Same class of gaps with no permanent guard:

- Command/action reference graph: preflight suggested actions
  (`workbench.datasetExperiment` etc., scientific_preflight.cpp), empty-state
  CTAs (`selection_context.cpp` commandId) and `main_window_workbench.cpp`
  registry lookups are stringly-typed; help ids add a `command.` prefix.
  Nothing proves the four projections agree beyond a partial source-scan in
  `test_help_coverage.cpp` (which scans `command_defs.cpp` only).
- Diagnostics: `DiagnosticCatalog::fallback()` silently covers unknown error
  codes, so a new error enum value never forces a curated page decision
  (#870 fixed by hand in data/help/diagnostics.json, guardless).
- HelpContentStore: duplicate top-level + recursive loading fixed in
  `f316dfdbb4` by deleting the first loop; no regression test pins the
  single-load behavior (#871).
- Contract knowledge is scattered across ~30 test files with different
  mechanisms; there is no single machine-readable contract inventory/graph.

## 6. Build environment (this host)

- 16 CPUs / 62 GB RAM; Ninja; GCC (`/usr/bin/c++`); Qt+QGIS+GDAL+OTB stack.
- Existing builds in the main checkout: `build/` (Release). This track's
  worktree will configure its own build directory to avoid touching other
  tracks. Build parallelism ≤ 4 (default 2), tests sequential.
