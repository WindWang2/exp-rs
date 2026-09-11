# PLAN — execution sequence (Verification Platform 8.0)

## Committed decisions

| # | Decision | Rationale |
|---|---|---|
| D1 | Primary local lane = Clang 22 Release (build-clang), GCC kept as documentation + best-effort | GCC 16.2.1 (system, 20260810) segfaults (ICE in diagnostic machinery) on random heavy vendored-QGIS TUs; three separate attempts failed on three different files. Clang built this exact tree before (main worktree build-clang). |
| D2 | `-fpermissive` in CXX flags mirrors the main worktree's local build policy | required for vendored QGIS on this GCC; recorded for reproducibility |
| D3 | Trace adapters are wrappers (commit→commitImpl) or funnel taps (flushPendingSignals, notifyRunStateLocked) | single reviewable insertion point, no control-flow change, disabled path untouched |
| D4 | Fault points take the REAL failure branch + explicit rollback where the transaction is open | fault_point.h contract: never fabricate success, never leave partial state |
| D5 | Lane L4 uses test_exprs_ipc (not the WIN32-only external-process test) | target exists only under WIN32; portable IPC coverage lives in test_exprs_ipc |
| D6 | Lane L6 visual tests run inside the combined test_mapspec binary | test_cartography_visual.cpp is a source of test_mapspec, not a target |
| D7 | Scale benchmark defaults: schedule 100k, dataset rows 100k, overridable via SICNU_BENCH_* env | goal asks for the 100k-class numbers; env bounds keep weak hosts safe |
| D8 | STAC pagination fuzzing NOT added in this track | STAC client lives in src/app (QGIS closure) and is io-track-owned; recorded as follow-up |

## Steps

1. Baseline audit + planning docs (done)
2. M1 hygiene + gdal_compat + portability contract (done, committed)
3. M2 ladder script (done; lane names fixed D5/D6)
4. M3 trace chain adapters + tests (code done; evidence pending build)
5. M4 fault points + tests (code done; evidence pending build)
6. M5 known-answer corpus 8 (code done; evidence pending build)
7. M6 fuzz ops/ipc (code done; evidence pending build)
8. M7 benchmark_scale8 (code done; evidence pending build)
9. M8 anti-vacuity: grep sweeps found legacy tests clean post-#814; added
   the anti-vacuity proof for the NEW TaskCenter trace adapter (real instant
   job through the single admission path, terminal record asserted)
10. M9 readiness report script (done; evidence pending build+lanes)
11. Ladder run (L0..L7) + benchmark JSON artifacts on this host
12. Adversarial review (2 subagents), remediation, REVIEW_LOG.md
13. FINAL_REPORT.md, push, PR
