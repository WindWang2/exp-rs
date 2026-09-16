# PARALLEL_OWNERSHIP — concurrent tracks vs this track

Snapshot at Phase 0 (2026-09-16). Re-audit before every rebase.

## Open PRs

| PR | branch | changed files (summary) | overlap with this track | policy |
|---|---|---|---|---|
| #1009 | `zcode/execution-runtime-convergence-11` | `src/runtime/**`, `src/operators/framework/**`, `src/processing/framework/local_worker_pool.*`, `src/workflow/pipeline_run_coordinator.cpp`, `src/agent/data_platform_tools.cpp`, `tests/test_*execution*`, `tests/CMakeLists.txt`, `CHANGELOG.md`, `.gitignore`, `data/help/diagnostics.json`, `docs/execution/**` | none on business files; shared: `tests/CMakeLists.txt`, `CHANGELOG.md`, `.gitignore`, `data/help/diagnostics.json` | read-only vs #1009; my edits to shared files are minimal append-only, deferred to integration commits; if a conflict appears at rebase, rebase first, and keep my additions strictly additive |
| #1008 | `zcode/radiometric-spectral-workbench` | `src/core/radiometric_state.*`, `src/core/spectral_library.*`, `src/analysis/{atmospheric,hyperspectral}/**`, `src/processing/algorithms/{radiometric_calibration,spectral_indices,spectral_unmixing}.*`, `src/agent/spatial_tools/spectral_spatial_tools.*`, `src/app/widgets/{band_composite_palette,spectral_profile_widget}.*`, new tests, `tests/CMakeLists.txt`, `.gitignore`, `src/agent/CMakeLists.txt`, `src/{analysis,app,core}/CMakeLists.txt` | none on business files; shared: `tests/CMakeLists.txt`, `.gitignore`, `src/agent/CMakeLists.txt` (needed only if I add a new src/agent test/file target — see below) | same as above; my src/agent CMakeLists edits are append-only lines for new `*lab*` files |

Risk note: #1008's spectral kernels overlap thematically with lab10 (hyperspectral)
content, but the lab's grading rules grade student ARTIFACTS with independent
assertion kernels — they do not import #1008 code, so no functional duplication.

## Merged since prompt snapshot (now master fact, re-audited)

- #991 D18 Unified Mission Workbench (merged c5d4aafe) — LabSpec/IR2 UI now master
  fact; this track does NOT modify `src/app` LabSpec UI.
- #992 D19 Dataset Foundry/Benchmark (merged 1cea9892) — `src/experiment`
  BenchmarkService etc. now master fact; this track consumes existing stable seams
  (ExperimentStore) but does not modify foundry/benchmark files.

## Open issues

#1001–#1007: all platform-domain defects (io/workflow/dataset/georef/agent-dataset
tools). None in this track's packages. Recorded in BASELINE.md as OUT_OF_SCOPE;
not fixed, not closed, not commented by this track.

## File-level write plan for THIS track

Primary (business body):
- `data/labs/**` (pack manifests, grading rules, pipeline refs)
- `docs/labs/**` (generated pages + authoring docs)
- `src/agent/lab_*` (new grader kernel module, pack manifest module, injection corpus)
- `src/experiment/bridge/lab_*` (grade embedding recorded path, report CLI bridge)
- `src/cli/lab_*` + `src/cli/cli_lab_commands.*` (batch 2.0, `--report`, `--self-check` flags)
- `scripts/*lab*` (fixture generators for new labs)
- `tests/test_lab_*` (new/extended suites)

Shared integration files (append-only, minimal, deferred to integration commits):
- `tests/CMakeLists.txt` (new test targets)
- `src/cli/CMakeLists.txt`, `src/experiment/CMakeLists.txt`, `src/agent/CMakeLists.txt`
  (new file targets)
- `.gitignore` (one whitelist line for `.planning/teaching-lab-platform-11/`)
- `CHANGELOG.md` (one entry)
- `data/help/*` (only if help catalog contract requires registration; check first)

Never touched: `src/app/**` (D18 UI), `src/runtime/**`, `src/operators/**`
platform code (ISSUES.md gaps), other tracks' `.planning/*` dirs.
