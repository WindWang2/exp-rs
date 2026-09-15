# PARALLEL_OWNERSHIP — file-level overlap with concurrent tracks

Audited at Phase 0 (2026-09-16). Re-audit required after every rebase.

## Open PRs at start

| PR | Branch | State | Overlap with this track | Policy |
| --- | --- | --- | --- | --- |
| #1009 execution-runtime-convergence-11 | `zcode/execution-runtime-convergence-11` | MERGEABLE, open | touches `.gitignore`, `CHANGELOG.md`, `tests/CMakeLists.txt`, `src/runtime/**`, `src/operators/framework/**`, `src/workflow/pipeline_run_coordinator.cpp`, `src/agent/data_platform_tools.cpp`, `data/help/diagnostics.json` | Shared integration files are append-only here. `data/help/diagnostics.json` overlap: they may add entries; my entries are new `diagnostic.env.*` ids appended at the end — rebase resolves trivially. I do NOT touch `src/runtime/**`, `src/operators/**`, `src/workflow/**`, `src/agent/**`. |
| #1008 radiometric-spectral-workbench | `zcode/radiometric-spectral-workbench` | CONFLICTING (already conflicts with master), open | touches `.gitignore`, `tests/CMakeLists.txt`, several `src/*/CMakeLists.txt`, spectral core | No file I touch is in its changed set except `.gitignore`/`tests/CMakeLists.txt` — my edits there are append-only at file end. |
| #991/#992 | merged | — | now part of master; audited as master state | n/a |

## My primary write scope (claimed exclusively by this track)

- `packaging/**` (OFFLINE_BUNDLE.md, bundle/VERIFY.sh new, bundle/VERIFY.ps1, build-appimage.sh)
- `scripts/build_offline_bundle.sh`, `scripts/bundle_manifest.ps1`, `scripts/offline_smoke.sh`, `scripts/verify_bundle_manifest.py` (new), `scripts/report_bundle_dependencies.py` (new), `scripts/cleanroom/**` (new)
- `scripts/windows/_env.cmd`, `scripts/windows/build_offline_bundle.cmd`-adjacent edits, `scripts/windows/bundle_dependency_report.ps1` (new)
- `docs/deployment/**` (lab-offline.md update, env-doctor.md new, MIGRATION.md new)
- `tests/fixtures/bundle_manifest/**` (new), new test files `tests/test_env_doctor.cpp`, `tests/test_bundle_manifest_conformance.*`-equivalent ctest wiring

## Justified scope extension (from primary list, documented in DECISIONS.md)

Work package E (first-run diagnostics) provably does not exist on master (BASELINE gap E). The
minimal implementation requires:

- `src/geospatial/doctor/env_doctor.{h,cpp}` (new files; Qt-free layer, no PR #1009/#1008 overlap)
- `src/geospatial/CMakeLists.txt` (append 2 source lines)
- `src/cli/cli_env_doctor.{h,cpp}` (new files), `src/cli/CMakeLists.txt` (append 1 source line)
- `src/cli/cli_commands.h` + `src/cli/cli_commands.cpp` (append-only: dispatch entry + declaration)
- `data/help/diagnostics.json` (append new `diagnostic.env.*` entries; existing 104 untouched)
- `tests/CMakeLists.txt` (append registrations)
- `.gitignore` (append 3-line whitelist for `.planning/deployment-packaging-11/`)
- `CHANGELOG.md` (append entry at Unreleased)

`src/cli/main_cli.cpp` is NOT touched by #1009/#1008; `cli_commands.cpp` is not touched by either.
No scientific algorithm code is modified.

## Dedupe checklist for open issues (#1001–#1007)

All seven are scientific-domain defects (io/workflow/dataset/georef/agent). None overlap
packaging/deployment. Evidence for "no silent update check" (F): `grep -rin "update.*check\|check.*update\|autoUpdate" src/` → no hits in packaging/release paths (re-verified in EVIDENCE).
