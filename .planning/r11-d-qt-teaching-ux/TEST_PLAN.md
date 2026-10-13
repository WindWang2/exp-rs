# Test Plan & Verification Oracles — Direction D

## Core Principles
1. Every bug fix must follow **RED → Root Cause Analysis → Minimal Surgical Fix → GREEN × 2**.
2. No deletion of tests, no weakening of assertions, no arbitrary sleep delays.
3. Automated test execution strictly at `CTEST_PARALLEL_LEVEL=1` with `QT_QPA_PLATFORM=offscreen`.

## Verification Suites & Targets

| Work Package | Test Target | Verification Oracle |
|---|---|---|
| **WP-D8 (Terminology)** | `test_terminology_provider` / `test_help_integrity_12` | 1. Duplicate English terms generate distinct suffixed descriptor IDs (e.g., `term_2`).<br>2. Related ID lookups resolve correctly without dangling.<br>3. Non-existent resource path records a typed error message in `LoadResult.errors`. |
| **WP-D1 / WP-D3 (UI & Vector)** | `test_selection_context` / `test_main_window_vector` | 1. `copyFeatures` / `cutFeatures` with empty selection emits clear status bar message instead of silent no-op.<br>2. `pasteFeatures` with empty or incompatible clipboard reports user-visible feedback.<br>3. `selectAll` on non-vector layer informs user.<br>4. Ribbon Vector Editing actions are disabled when layer is not editable. |
| **WP-D4 (Pipeline Editor Save)** | `test_workflow_pipeline_ui` | 1. `writePipelineDefinitionFile` writes atomically via temp file + rename.<br>2. Write failure preserves original target file without leaving a truncated or empty file. |
| **WP-D5 / WP-D9 (Experiment Studio)** | `test_experiment_studio_core` / `test_experiment_studio_dock` | 1. Loading 1,000+ run matrix rows operates cleanly without excessive memory overhead.<br>2. Filter by `pointId` updates displayed rows accurately. |
