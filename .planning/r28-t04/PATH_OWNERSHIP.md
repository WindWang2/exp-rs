# Path Ownership (R28-T04)

## Write (this track)
- `src/app/**` — **excluding `src/app/agent_ops/`** (T03)
- `src/app/workflow/**` (canvas), `src/app/shell/workflow_session_controller.*` (explicitly T04 even though shell shared)
- `src/ui/**`, `resources/styles*.qss`, `src/app/design_tokens.h` (sync with QSS headers only where my changes require)
- `src/agent/cartography/**`, `src/agent/mapspec/**`, `src/agent/layout_tools/**`, `src/agent/symbology/**`
- `src/app/layout/**` (F12: our fork's layout designer dialog)
- cartography data/templates: `data/cartography/**` (template fixtures only if a fix demands it — none planned)
- tests: only `tests/test_r28_t04_*.cpp` (new) + my own suites' registrations appended with `# ── R28-T04` marker; existing shared files edited only in my own case blocks
- docs: `docs/development/r28/t04/**`

## Read-only reliance (other tracks own)
- `src/geospatial/**`, `src/data/**` (ImportPreflight/ImportSession API — consumed as-is), scan pool (`src/app/display/scan_pool.*` if shared app-side utility, verify actual owner)
- `src/app/agent_ops/**` (T03), `src/teaching_admin/**` + workflow execution backend (T05)
- `src/core/**` / `src/gui/**` QGIS fork internals: touch only for T04-scoped wiring bugs (F12 is in our custom `src/app/layout/` designer; upstream-forked `src/core` changes need strong justification)

## Shared hotspots (governance per contract §D)
- `tests/CMakeLists.txt` — append-only block with track marker; keep every existing target
- `resources/translations/sicnu_zh_CN.ts` — PR #1813 also edits; keep hunks minimal, re-diff before merge
- `src/app/CMakeLists.txt` — new files for panels/dialogs only

## Verified-untouched by #1813 (open PR) in the logic I change
- `orthorectification_dialog.*` (F7) — not in #1813 file list
- `src/app/layout/qgslayoutdesignerdialog.cpp` (F12) — not in list
- cartography_dock lifetime code (F14) — #1813 touches the file but only buildUi layout rows
- experiment_studio_dock teardown (F18) — same, layout-only hunks
- atlas/produce/export in `src/agent/cartography/**` — not in list
