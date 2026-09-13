# PLAN — LabSpec Data-Driven Labs

Worktree: `/home/kevin/projects/rs-studio/exp-rs-lab-spec-data-driven` · Branch: `zcode/lab-spec-data-driven`
Build: `build/` (Ninja, Debug) · hard caps `-j2` / `CTEST_PARALLEL_LEVEL=1`.

## Phases

| # | Phase | Key outputs | Status |
|---|-------|-------------|--------|
| 0 | Baseline audit | `EVIDENCE.md`, `CAPABILITY_MATRIX.md` | in_progress |
| 1 | A: LabSpec schema | `data/schemas/labspec.schema.json`, `.gitignore` exception, `data/labs/` layout | pending |
| 2 | B+C: loader + widget + operator-bound steps | `src/app/widgets/lab_spec_loader.{h,cpp}`, `guided_workflow_widget.{h,cpp}` refactor, delete 10 factories | pending |
| 3 | E: canonical lab set | `data/labs/lab01..11*.lab.json` (≥10) | pending |
| 4 | D: doc generator | `scripts/gen_lab_docs.py`, regenerated `docs/labs/` | pending |
| 5 | F+G+H: guards/tests/docs | `tests/test_labspec.cpp`, behavioural `test_guided_workflow_widget.cpp`, `docs/labs/LABSPEC.md`, `docs/adr/0146-labspec.md` | pending |
| 6 | Adversarial review | ≤2 read-only subagents over full diff | pending |
| 7 | Remediation | P0/P1 → 0 | pending |
| 8 | Rebase + PR | rebase origin/master, push, `gh pr create`, report URL, stop | pending |

## Key architecture facts (audit-verified)

- App = single executable target `sicnu_geo_rs` (src/app/CMakeLists.txt:99); widget at
  `widgets/guided_workflow_widget.cpp` in its source list (line 205).
- Operator registry: `sicnu::processing::AtomicAlgorithmRegistry` (`findAdapter(id)`,
  `listDescriptors()`), populated from `sicnu::operators::RSOperatorRegistry` — 110 `rs:*` ids.
- Param validation shared seam: `validateParameters(params, desc, UnknownParameterPolicy::Error)`
  in `src/processing/framework/schema_validator.h` → drift guards reuse it.
- Execution seam identical to headless: `sicnu::jobs::JobRequest{algorithmId, params}` via
  `sicnu::app::GuiJobHandle::submitJob` (src/app/shell/gui_job_adapter.h) — same as all dialogs
  (`runOperatorTask`, raster_processing_dialog_base.cpp:492).
- Data dir resolution: `sicnu::processing::resolveRuntimeDataPath("data/labs")`
  (header-only `src/processing/framework/runtime_paths.h`; `SICNU_DATA_DIR` override for tests).
- UI verbs on QgisDesktopWindow are `public slots` (main_window.h:207-323) — invokeMethod OK.
- Test helper `sicnu_add_test()` (tests/CMakeLists.txt:48) links sicnu_processing/operators stack;
  `test_guided_workflow_widget` is currently a bare Qt-only target (line 5258).
- jsoncpp (`json/json.h`) is the JSON lib used app-wide.

## Design (B+C)

- New `src/app/widgets/lab_spec_loader.{h,cpp}`: pure Qt Core + jsoncpp; structs
  `LabSpec { LabStep }`; `loadLabsFromDir()`, typed `LabSpecError` (path, labId, line, reason);
  structural validation mirroring `labspec.schema.json`; no fallback content.
- `GuidedWorkflowWidget`: owns `GuiJobHandle`; `loadWorkflows()` scans `data/labs/*.lab.json`
  via `resolveRuntimeDataPath`; load errors surface as a typed error entry in the list + log;
  `WorkflowStep { operatorId, params(Json::Value), action }` — operator steps run via
  `JobRequest` (identical to headless), pure-UI steps via invokeMethod `action`.
- Factories deleted after parity; `.gitignore` gets `!data/labs/` + `!data/labs/**`.

## Canonical lab set (E) — union, dedup, orphans promoted

lab01 image_enhancement · lab02 spectral_analysis · lab03 classification · lab04 change_detection ·
lab05 terrain_analysis · lab06 georeferencing (docs-only) · lab07 image_fusion ·
lab08 atmospheric_correction (widget-only) · lab09 pca_analysis · lab10 mosaic · lab11 obia_classification
(widget orphans promoted). Zero-padded numbers for stable sort; docs filenames regenerate accordingly.

## Errors / lessons log

| Error | Attempts | Resolution |
|-------|----------|------------|
| zsh `--include=*.h` / `echo ===` glob-expansion errors | 2 | quote patterns; use `grep -n` with quotes |
