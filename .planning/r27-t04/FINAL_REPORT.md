# R27-T04 Final Report — Qt/QGIS 遥感工作台：可靠交互、绘制/谱学/分类、项目保存及 UI 性能

- **Base SHA**: `10652fd003c66ae78598d2b878eed9beb50029fa` (origin/master, fetched 2026-11-02T16:24:58Z)
- **Head SHA**: see `git rev-parse HEAD` at PR creation (branch `agent/r27-t04-qt-qgis-workbench-ui-20261102T162559Z`)
- **Worktree**: `../exp-rs-r27-t04-20261102T162559Z` (sole track worktree; fresh from BASE_SHA)

## Issues handled (real-time triage at base; all fixes verified at head)
| Issue | Outcome |
|---|---|
| #1745 ROI tool double-free/UAF (P1) | FIXED — deferred-only release + callback self-guard; 100× re-invoke + teardown stress green |
| #1746 orphan classId heap OOB (P1) | FIXED — rebuild/recompute identity gates + defense-in-depth + transactional merge abort; skip visible in status bar |
| #1747 downgrade save strips SICNU block (P1) | FIXED — `canWrite()` preflight in saveProject/saveProjectAsTo/close-save; on-disk file byte-identical after refusal (pinned by test) |
| #1788 STAC More replaces (P2) | FIXED — append semantics |
| #1789 stranded More button (P2) | FIXED — `searchNextUnavailable` terminal signal + full state machine settlement |
| #1790 cross-section GUI-thread GDAL (P2) | FIXED — scan-pool worker, slot p95 0.011 ms |
| #1791 magic-wand GUI-thread read+fill (P2) | FIXED — snapshot/worker/landing pattern, slot p95 0.016 ms |
| #1792 export remove-before-copy / dest==src (P2) | FIXED — staged-atomic publish; canonical + ancestor-dir + symlink-dir refusals |
| #1793 pipeline JSON abort (P2) | FIXED — stackLimit + typed catch; + same-class hardening in STAC item parse (R1) |
| #1794 classification scanline re-read on GUI thread (P2) | FIXED — off-thread recompute, slot p95 ≤1.24 ms; mean-bias fix included |
| #1795 sidecar deleted before dual-channel persist (P2) | FIXED — dated recoverable backup, keep-newest sweep |
| #1765 STAC COG bypasses SSRF (P2) | FIXED at the T4 call site — GUI engages the existing process private-network policy (SICNU_ALLOW_PRIVATE_NETWORK opt-out) |
| #1561 spectral profile sync GDALOpen (P2) | **FIXED_IN_MASTER** (923595d0e2 worker sampling for network sources) — not reimplemented |
| #1805 F17/F13/F6 (P3) | FIXED (NaN guard; geotransform cache; CGImageRelease — macOS test NOT_EXECUTED). F7/F12/F14/F18 deferred (documented) |

## Verification summary (all locally executed)
- Build: configure+targets exit 0 (Debug, -j2/-j1 cap, host-wide flock; transient toolchain ICE/ld-SIGSEGV retried — pre-existing flakiness).
- New/changed suites: 13 (12 × `test_r27_t04_*` incl. bench + adapted `test_roi_tool_magicwand`). **Final post-R2 double run: 13 × 2 consecutive passes, all exit 0.**
- Regression sweep: 8 affected existing suites all pass (stac_client, mission_context, workspace_project_v3, roi_spectrum_tool, spectral_profile_widget, r26_workbench_classification_ux, classification_window, interactive_session_contract).
- Bench (measured, ×2 stable): recompute slot p50/p95 ≈ 1.06/1.24 ms (e2e 18–22 ms); cross-section 0.006/0.011 ms (e2e 4.3 ms); magic-wand 0.014/0.016 ms (e2e 362–424 ms). HEAD slot cost ≥ e2e by identity (same work, same machine). No unmeasured perf claims.
- Reviews: R1 (11 findings: 1 out-of-whitelist S1 ledgered, 10 fixed) → re-run ×2 green; R2 (branch judged sound; 4 S3: 2 fixed, 1 ledgered, 1 history-hygiene noted) → final re-run ×2 green.

## Whitelist audit
Production diff paths ⊂ `src/app/**` (no agent_ops), `src/native/mac/qgsmacnative.mm`, `tests/**`, `.planning/**`. No src/geospatial, src/runtime, src/python, docs/adr, root CMakeLists, .gitignore changes. `tests/CMakeLists.txt`: master's file did not even parse at base (interleaved merge damage) — canonical repair cherry-picked from sibling track r27-t01 (`d66962c615`, identical content both branches) + append-only R27-T04 block; 3-way re-check done at PR time.

## Open / redirected (honest)
- `src/python/sicnu_python_api.cpp saveProject()` still calls `QgsProject::write()` unguarded (pre-existing; outside this track's whitelist) — the #1747 guarantee covers every in-shell path; flagged for the owning track.
- `src/geospatial/remote/process_network_policy.cpp` refusal text mentions the CLI flag only (cosmetic, out of whitelist).
- STAC dialog `searchDropped` does not restore the Search button (unreachable with the dialog's stack-scoped lifetime).
- Two early commits' messages under/over-describe content (noted; history rewrite rejected).
- macOS/Windows runtime, real-network STAC, visual smoke on a display: NOT_EXECUTED (documented in TEST_RESULTS.md).
