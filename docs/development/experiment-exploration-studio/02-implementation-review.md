# Implementation review — Experiment Exploration Studio

Date: 2026-09-22 (Asia/Shanghai)
Baseline: `a9dc33fa` + this branch
Reviewer: implementation pass (self)

## Architecture

```
src/experiment_studio/     Qt Core projection (Sicnu::experiment_studio)
  study_designer           A — operator schema → ParameterStudySpec + guardrails
  run_matrix_projection    B — StudyReport rows / filters / identity
  spatial_compare_projection C — study_spatial; mismatch = typed refusal
  sensitivity_projection   D — curves / envelopes / Pareto / replicate flag
  fault_teaching_projection E — predict→diagnose; sandbox fingerprint contract
  first_divergence_projection F — debugger report → teaching VM + confidence downgrade
  studio_export / session  G — offline JSON+CSV bundle + session state

src/app/experiment_studio/ Qt UI (dock tabs A–G, QPainter chart)
```

**Honesty:** no second sweep, no private thread pool, no silent resample.
Cancel UI signals TaskCenter path only (placeholder message in dock).

## Files touched (owns)

- `src/experiment_studio/**` (new)
- `src/app/experiment_studio/**` (new)
- `tests/test_experiment_studio_core.cpp` (new)
- `docs/development/experiment-exploration-studio/**`
- Append-only: root `CMakeLists.txt`, `src/app/CMakeLists.txt`, `tests/CMakeLists.txt`
- Minimal registration: `main_window.h`, `main_window_workbench.cpp`, `command_defs.cpp`

## Parallel #1237

Did **not** touch `src/teaching/**`, `src/app/teaching/**`, or teaching tests.
Shared hotspots only appended Studio command/dock beside DatasetExperiment.

## Fixes during implementation

1. Unqualified `sicnu::study::*` types inside `experiment_studio` namespace → qualified / using.
2. `command_defs` initially nested Studio RS_CMD inside dataset block → split into separate register blocks.
3. `DiagnosticSeverity` (not nested enum) for studio errors.

## Residual risks / follow-ups

- Dock demo summarizer handles `synthetic://` only; production path = `GdalRasterDifferenceSummarizer` (already in study).
- Full `sicnu_geo_rs` link not required for projection DoD; dock objects compiled when app target is built.
- Live ExecutionPlaneStudyBackend wiring in UI is intentional seam (Run Matrix uses synthetic report for offline teaching); production submit remains study_bridge.
- Offscreen GUI Catch2 dock smoke deferred (needs qgis_gui); projection suite covers contracts.
