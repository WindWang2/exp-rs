# Acceptance (R28-T04)

## Scope decision (recorded in DECISIONS.md too)
This track's verified, evidence-backed defects (BASE_SHA `50cc3255c5`):
1. **WP10 atlas**: `#1812` names "cartography atlas deliveries" as failing on master — reproduce on local build, fix root cause, no expected-file relaxing.
2. **WP02 leftovers of #1805**: F7 (GDALOpen on GUI thread in orthorectification dialog), F12 (template load failure wipes layout), F14 (cartography dock per-job lifetime leak), F18 (experiment studio destructor join ≤60 s GUI block). All four CONFIRMED_OPEN in source.
3. **WP03**: `ImportPreflight` (src/data, T02, shipped) is referenced **nowhere** in `src/app/` — the generic Add-Raster/Vector loop registers synchronously and reports only "N succeeded, M failed" with no per-item reason, no needs-choice interaction. Build the batch summary + per-item explanation + skip/refuse UX on the existing API.
4. **WP05/WP04/WP07/WP11**: verification-first — run the existing suites (`test_workflow_session_controller`, `test_r27_t04_*`, `test_r25_d_*`, schema-form/TaskPanelHost suites), extend only where a real gap shows; no speculative rewrites. #1811 already closed the P1/P2 data-safety issues (#1745-47, #1788-95); I re-run their suites as regression, not re-fix.
5. **WP08**: no visual restyle (PR #1813 owns that lane); only fix contrast/spacing defects my own changes introduce.

## Definition of done per work package
| WP | Done when |
|---|---|
| WP01 | State matrix doc `docs/development/r28/t04/entry-state-matrix.md` for the flows I touched (import, ortho, template load, cartography produce, study teardown) with enabled/reason/undo columns — from actual code paths + offscreen app run where possible |
| WP02 | F7/F12/F14/F18 fixed; regression suites authored to fail pre-fix; two consecutive green runs; #1561 spot-verified via existing `test_spectral_profile_widget` (network path) |
| WP03 | Add-Raster/Vector shows preflight batch summary (verdicts, per-item findings, dedup note); NeedsUserChoice items require explicit choice or skip; refused items never registered; 100% on new suite |
| WP04/05 | Existing suites green ×2; any newly-found desync gets a minimal fix + case; else ALREADY_DONE_WITH_EVIDENCE recorded |
| WP07 | Map-tool/shortcut consistency spot-checked via existing suites; gaps → minimal fix or HANDOFF |
| WP10 | Atlas failures reproduced (exact assertion + log), root-caused, fixed, both atlas cp11 cases + AG-M cases green ×2; delivery files re-opened & hash-verified by the tests |
| WP11 | #1811's project-safety suites re-run green; no new defect found; else minimal fix |
| WP12 | Real app binary launched offscreen (QT_QPA_PLATFORM=offscreen) for import→produce flow screenshots where feasible; NOT_EXECUTED marked for visual-only checks |

## Exit criteria (contract §J)
- All above suites pass twice consecutively on this worktree's build; zero new failures elsewhere in the families I touch (`cp11`, `agm`, `r27_t04`, `r25_d`, workflow/session, shell lifecycle).
- R1 independent review (fresh context) → all P0/P1 + reproducible P2 fixed → R2 independent re-review, no unresolved blockers.
- Re-fetch master + PR #1813 state before PR; merge master; `scripts/check_merge_registration_integrity.py` + fresh configure pass; regression re-run.
- PR with full evidence body; issues referenced accurately (`Refs`/`Fixes` per actual sub-item state).
