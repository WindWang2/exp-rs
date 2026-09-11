# OWNERSHIP — scientific-mlops-9

## This track OWNS (may add/modify freely)

- `src/dataset/` — split engines, leakage/fold audits, dataset version
  identity/lineage, sample & annotation governance metadata.
- `src/experiment/` — ExperimentStore, run recorder/bridge (data side),
  scientific evaluation, comparison, reproduction bundle/readiness,
  experiment matrix, replay promotion seam.
- The **data side** of the execution→experiment bridge (the bridge lives in
  `src/experiment/`; the workflow-side adapter in
  `src/workflow/workflow_run_coordinator.*` / `src/app` integration points
  are shared seams — see below).
- CLI/MCP data surfaces for dataset/experiment/reproducibility verbs
  (`src/cli/cli_dataset_commands.cpp`, `src/agent/**/data_platform_tools*`).
- `tests/` targets covering the above (new `test_mlops9_*` suites and
  extensions of `test_dataset_*`, `test_split_*`, `test_experiment_*`).
- `.planning/scientific-mlops-9/`.

## Shared seams (modify only with minimal, additive edits)

- `src/workflow/workflow_run_coordinator.{h,cpp}` — the CLI auto-record
  follow-up touches the pipeline runner seam. The execution-concurrency-9
  track is actively remediating this file for #860/#876. **Policy**: this
  track does NOT rewrite coordinator internals; CLI auto-record is wired at
  the runner/monitor level with a small, additive hook, or is re-scoped to
  a follow-up if the seam moves under us (documented in FINAL_REPORT).
- `CMakeLists.txt` files — additive target registration only.
- `CHANGELOG.md` — one entry at final milestone only.
- `.gitignore` — planning-dir allowlist (already done, minimal).

## This track MUST NOT touch

- `src/processing/`, `src/geospatial/` (scientific-algorithms-9 /
  geospatial-data-fabric-9 tracks)
- Execution scheduler internals: TaskCenter/JobEngine executor core
  (exec-concurrency-9 track owns the current defect wave there)
- `src/app/` GUI widgets, `src/gui/`, QGIS display/renderer (workbench /
  exec tracks)
- Model runtime internals (`src/runtime/`, model catalog internals —
  M8 integrates through the existing stable catalog interface only)
- `src/plugins/`, `src/sdk/`, `src/help/`

## Authority boundaries honored (goal §0)

- Pi remains the only agent loop/runtime; no second scheduler.
- `WorkflowRunCoordinator → TaskCenter → JobEngine → Operator` remains the
  only execution chain; the experiment matrix (M5) submits runs THROUGH it.
- Dataset/Experiment stores remain the only metadata authorities.
- The recorder never executes work (M3); comparison never hides identity
  differences (M6); replay goes through the existing execution chain (M7).
