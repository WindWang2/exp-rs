# PARALLEL_OWNERSHIP — linked-visual-analytics-11

Snapshot at start (2026-09-16, master = a5b11b7f). Re-audit on every rebase.

## Open PRs

### PR #1009 `zcode/execution-runtime-convergence-11` (open, UNSTABLE)
Owns: src/runtime/**, src/operators/framework/**, src/processing/framework/**,
src/workflow/pipeline_run_coordinator.cpp, src/agent/data_platform_tools.cpp,
docs/execution/**, data/help/diagnostics.json, CHANGELOG.md,
tests/test_*execution*/test_chunk_*/test_worker_lease*.
**File-level intersection with this track: NONE** in business code.
Shared integration files to keep append-only: CHANGELOG.md (only if we touch
it — currently NOT touched), tests/CMakeLists.txt (append new test targets at
end), .gitignore (append 3 whitelist lines).

### PR #1008 `zcode/radiometric-spectral-workbench` (open, DIRTY)
Owns: src/app/widgets/{band_composite_palette,spectral_profile_widget}.*,
src/core/{radiometric_state,spectral_library}.*, src/analysis/**, src/processing
/algorithms/{radiometric_calibration,spectral_indices,spectral_unmixing}.*,
src/agent/spatial_tools/spectral_*, docs/adr/0158, related tests.
**File-level intersection: src/app/CMakeLists.txt + tests/CMakeLists.txt
only** (they add sources/targets; our additions are separate lines → merge is
textual-append on both sides). No business-code overlap.

### Merged during track lifetime (re-audit triggers)
- #991 mission workbench → merged (c5d4aafe): main_window/mission_context now
  master fact; used as seam, not avoided.
- #992 dataset foundry → merged (1cea9892): no VA impact.

## This track's primary writes

- src/app/visualanalytics/** (new: va_selection_hub, va_layer_link_controller,
  va_cursor_probe; modified: va_workbench_panel, va_chart_widget (hover
  signal only), va_source untouched)
- src/app/shell/view_link_controller.* (upgrade: groups/history/cursor)
- src/app/display/qgis_display_manager.{h,cpp} (ONE additive signal emit —
  see DECISIONS D3; #1008/#1009 own nothing here)
- main_window.h / main_window_view.cpp / main_window_workbench.cpp (minimal
  wiring; nobody else owns these right now)
- src/app/workbench/command_defs.cpp (append view.* block),
  data/help/commands.json (append entries)
- src/app/CMakeLists.txt, tests/CMakeLists.txt (append-only)
- tests/test_view_link.cpp, tests/test_visual_analytics.cpp,
  tests/test_view_commands.cpp (new)
- docs/workbench/visual-analytics-linking.md (new),
  docs/ui-architecture.md (§31/§33 sync), .gitignore (3 whitelist lines)

## Conflict protocol

On rebase conflict: re-read the other branch's changed files, keep their
semantics, re-apply ours minimally; never ours/theirs-bulk; never duplicate
their feature under a new name. After any rebase: rebuild + rerun targeted
gates (two passes at the end).
