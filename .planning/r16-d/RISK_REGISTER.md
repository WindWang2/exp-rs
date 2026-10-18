# R16-D Risk Register

| # | Risk | Likelihood | Mitigation |
|---|------|-----------|------------|
| R1 | #1517 merges first → conflicts in src/app (main_window_vector.cpp touched by #1512 only, not #1517; ribbon_controller not in #1517) | low | write set verified disjoint from #1517; if master moves, rebase onto new master and re-run targeted tests before PR |
| R2 | WP-T07-1 lease fix changes long-standing behavior; other call sites assume lease inert | medium | route ALL commit/rollback paths (confirmSaveEdits, checkUnsavedChanges, toggleEditing stop, saveEdits) through the lease; search call sites of hasActiveEditLease/acquireEditLease; run full test_r14_D_vector_edit_honesty + test_qgis_display_manager families |
| R3 | WP-T07-1+F02 UAF: holding the lease makes assetChanged→relocateLayer destroy the editing layer | certain if R2 done naively | relocateLayer must skip editable layers (defer swap until session end) — both land in ONE commit with ASAN-free targeted runs |
| R4 | ribbon_controller.cpp outside literal T07 glob | low | sanctioned: not in #1517 diff, not in forbidden list, minimal surgical change (reason channel only) |
| R5 | T08 chart validation changes could reject previously-passing specs (false positives) | medium | only add problems for genuinely wrong-typed JSON (isString checks); run full test_mapspec + production_11 + quality families GREEN×2 |
| R6 | per-page geometry changes (WP-T08-6) could shift existing layout outputs | medium | page-0-only documents (the overwhelming default) must produce byte-identical digests/layouts; add digest-stability assertion for single-page specs |
| R7 | concurrent r16-a/b/c agents hold build lock; wall-clock stretch | high | generous flock timeouts (60-90 min), background builds, parallel doc/audit work meanwhile |
| R8 | catch2 fetch blocked by dead proxy | occurred | FETCHCONTENT_SOURCE_DIR_CATCH2 local cache (same as sibling builds) |
| R9 | memory pressure if all agents build simultaneously | medium | lock serializes full builds; monitor RSS, drop to -j2 if >70% |
| R10 | T07 tests driving real QgisDesktopWindow offscreen may be environment-sensitive | medium | baseline GREEN×1 recorded on clean master FIRST; any later failure A/B-compared against baseline binaries |
