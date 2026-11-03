# R27-T04 Review R1 (adversarial, independent reviewer)

Reviewer: isolated subagent (Qt lifetime / GIS workflow / persistence+perf lenses); reviewed full production diff 10652fd003..HEAD plus tests. Pre-review S1–S3 fixes re-verified.

## Findings and disposition

| # | Sev | Finding | Disposition |
|---|---|---|---|
| 1 | S1 | `src/python/sicnu_python_api.cpp:42` `saveProject()` calls `QgsProject::write()` unguarded — the #1747 downgrade loss is reachable from the embedded Python console | **Out of track whitelist (src/python/**)** — cannot edit from this track. Recorded here + in FINAL_REPORT/PR body as a pre-existing follow-up for the owning track (guarantee is enforced for every in-shell save path: saveProject, saveProjectAsTo, close-path write). |
| 2 | S2 | `selectCogHref` parses catalog-controlled JSON unbounded (the #1793 vector inside this PR's own file) | **FIXED** — stackLimit=64 + typed catch → typed refusal |
| 3 | S2 | dest==src through a symlinked directory bypasses the canonical check (dest not existing ⇒ empty canonical); staged==src pathological name | **FIXED** — resolve existing ancestor dir for dest; guard staged==canonicalSrc |
| 4 | S3 | backup sweep glob breaks on project names with glob metacharacters | **FIXED** — manual prefix+suffix match keyed by ms timestamp |
| 5 | S3 | More button visible-disabled after hide→reopen with dropped fetch | **FIXED** — searchDropped settles the button too |
| 6 | S3 | cross-section cancellation every 128 samples too coarse for remote sources | **FIXED** — every 16 |
| 7 | S3 | recompute test comment claims an unexercised post-destruction assert | **FIXED** — comment softened to what runs |
| 8 | S3 | staging-residue scan spans the whole temp dir (false-fail risk) | **FIXED** — scoped to dest name pattern |
| 9 | S3 | `mRasterGtValid` dead state | **FIXED** — removed; failed opens now reset the transform (stale-transform guard added) |
| 10 | S3 | (pre-existing, #1745 topic) spectrum callback released whatever `m_roiSpectrumTool` pointed at — a re-invoke in the watcher→delete window silently deleted the NEW tool | **FIXED** — shared-QPointer self-guard: the callback releases only the tool that fired it |
| 11 | S3 | (untouched file) geospatial policy refusal text mentions the CLI flag, misleading in GUI | Out of whitelist — recorded for the owning track |

## Verified clean (reviewer's summary)
#1745 release paths enumerated leak-free; #1746 transactional merge + gates + mean-bias fix; #1747 all in-shell write callers guarded, canWrite/write parity exact, close refusal leaves Discard possible; #1788/#1789 all interleavings enumerated sound; workers plain-value-only, destructor-cancels ordered before the pool destroyed-hook, bounded landings; #1792 staged-atomic core; #1793/#1795/#1765 discipline; P3 items; **whitelist PASS**; test honesty PASS (no tautologies/skips).

## Post-fix verification
Rebuilt (one transient `ld` SIGSEGV retried at -j1 — pre-existing toolchain flakiness). Full post-R1 double run: 13 suites × 2 runs all exit 0 (incl. test_stac_client regression for finding 2); bench 3/3 green (slot p95 ≤ 1.11 ms unchanged).

Reviewer coverage limits (stated by reviewer): code-only (no build/run); geospatial transport, atomic_fs internals, WorkspaceService internals, RsScanPool taken as given; macOS/Windows/remote-latency runtime not exercised; .planning docs not audited.
