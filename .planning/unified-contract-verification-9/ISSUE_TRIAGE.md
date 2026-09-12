# ISSUE_TRIAGE — re-verified on `origin/master` = `f316dfdbb4` (2026-09-12)

All eight historical drift issues are CLOSED (batch-fixed by `f316dfdbb4`).
Triage below records what the master fix actually did and whether a
permanent guard now exists; the "guard" column is this track's M0-M4 work.

| Issue | Title (state) | Verdict on master | Root cause on latest master | This track's action |
|-------|---------------|-------------------|-----------------------------|---------------------|
| #869 | Shell command drift in Unified Help (closed) | `fixed-by-later-merge` — f316dfdbb4 added 4 missing `command.workbench.*` entries to `data/help/commands.json` | Help command ids and shell command ids are kept in sync by hand; `test_help_coverage` scans `command_defs.cpp` only, not `main_window_workbench.cpp` registrations nor `selection_context.cpp` CTAs | M3: full command reference graph (registry ids ↔ help `command.*` ↔ preflight actions ↔ empty-state commandIds ↔ ribbon/menu refs) |
| #870 | Operator error code drift in diagnostic catalog (closed) | `fixed-by-later-merge` — diagnostics.json entries added | `DiagnosticCatalog::fallback()` silently covers new error enum values; nothing forces a curated-page decision per new code | M3: mechanical census — every value of RSOperatorError / HarnessError / model-runtime codes must resolve to a curated diagnostics page or an explicit allow-list entry with reason |
| #871 | HelpContentStore double-loading / duplicate ID collision (closed) | `fixed-by-later-merge` — duplicate top-level loop deleted in `loadFromDirectory` | Behavior pinned only implicitly; no regression test proves single-load + duplicate-id rejection | M3: regression test — load a temp dir twice-nested content, assert each help id registered exactly once and errors reported for true duplicates |
| #872 | `rs:infer` schema drift (closed) | `fixed-by-later-merge` — `device` added to schema() | Implementation reads `params["device"]`; schema hand-written; no impl↔schema equality check exists | M2: operator param scanner + set-equality test (headline guard) + mutation test |
| #879 | Fusion `msWeights` schema drift (closed) | `fixed-by-later-merge` — msWeights array schema added | same as #872 | M2 (same guard) |
| #880 | `io:inspect` `includeStatistics` schema drift (closed) | `fixed-by-later-merge` — boolean param added | same as #872 | M2 (same guard) |
| #881 | Preflight blocker action drift (closed) | `fixed-by-later-merge` — suggested actions renamed to real command ids (`workbench.datasetExperiment` etc.) | Suggested-action ids are free strings; validity depends on CommandRegistry + help data staying aligned; semantics owned by harness track | M3: closed action-id vocabulary test — every preflight suggested action resolves in the command id set derived from command_defs.cpp + workbench registrations, and has help content |
| #882 | Workbench empty-state action/shortcut mismatches (closed) | `partially-fixed-by-later-merge` — registry lookups + help entries landed; **no empty-state widget change in f316dfdbb4**; UI semantics belong to Workbench track per goal §4 | selection_context.cpp `commandId` strings and shortcut ownership | M3: coverage gate (CTA commandId ↔ registry ↔ shortcut owner); any residual UI mismatch → failing contract test + REVIEW_LOG entry, not a UI rewrite |

## Other issue classes in scope

- "implementation accepts X but schema omits X" generally → M2 guard makes
  the whole class mechanically red at test time.
- `test_algorithm_meta_drift.cpp` brittle `== 29` count pin → M4 replaces
  the count with derivation from live descriptors (no behavior change to
  product code; test-side strengthening only, keeping the existing suite
  green).

## Verification mapping

Each row above lands in `TEST_MATRIX.md` with the owning test name and the
"old code must fail / new code must pass" mutation evidence.
