# R27-T04 Tasks (each slice = own commit, RED→GREEN)

## Phase A — P1 memory/data safety
- [ ] A1 (#1745) RED: test re-invoking ROI tool while polygon unfinished (unsetMapTool+delete) survives event-loop processed deferred deletes; GREEN: `activateRoiSpectrumTool` never sync-deletes after `unsetMapTool` (deleteLater only, idempotent).
- [ ] A2 (#1746) RED: recompute with orphan classId ROI (via rebuild-from-sample-layer path) must not OOB (ASan/UBSan or guarded-write detection + curve honesty); GREEN: contains-guard before accumulate + orphan filtering at rebuild + merge rollback keeps classDefs consistent (no drop when commit rolled back) + status message for skipped orphans.
- [ ] A3 (#1747) RED: v3 project + unavailable store + save → on-disk extension block byte-identical after refusal; GREEN: `DataProjectSerializer` gets static `canWrite(context)` preflight; `saveProject`/`saveProjectAsTo` refuse before `QgsProject::write()` (no overwrite), user sees diagnostic.

## Phase B — GUI async
- [ ] B1 (#1790) CrossSectionWidget: generation stamp + QThreadPool worker (mirror RoiStatisticsWidget pattern), cancellation, NaN/empty landing.
- [ ] B2 (#1791) Magic wand: snapshot path+geotransform+click on GUI; RasterIO+flood fill on worker; queued apply via QPointer to tool; re-click supersedes.
- [ ] B3 (#1794) classification recomputeSpectralCurves: move GDAL work to worker (generation + no GUI-object access in worker), keep curve landing honest; cache nothing across source changes; p95 slot-time measured before/after (BENCH).

## Phase C — interaction correctness
- [ ] C1 (#1788) STAC dialog: append mode for More Results (first page replaces, next pages append); featureData consistent with rows; empty next page keeps table and hides More.
- [ ] C2 (#1789) StacClient::searchNext no-op emits completion (dedicated signal or documented completion with empty features + sentinel); search() error paths clear next state; dialog re-enables/hides More correctly in all terminal states.
- [ ] C3 (#1792) copyPathWithDialog: refuse dest==src (canonical paths), staged temp copy + atomic publish (`atomic_fs` house pattern), no remove-before-copy.
- [ ] C4 (#1793) readPipelineDefinitionFile: `stackLimit=256` + typed catch → user-facing error, no abort.
- [ ] C5 (#1795) persistMissionContextWithProject: stale sidecar renamed to recoverable backup instead of deleted; both-channel failure leaves original recoverable; restore still ignores backups.

## Phase D — STAC SSRF
- [ ] D1 (#1765) GUI entry engages `engageProcessPrivateNetworkBlocking()` (unless explicit opt-out env), mirroring CLI semantics; offline gate unchanged; test: after engage, a raw /vsicurl/ open of a private address is refused.

## Phase E/F — deepening + verification
- [ ] E1 (#1805 capacity-gated): F17 temporal NaN guard (≤16 width), F6 CGImageRelease (code-only, NOT_EXECUTED test note), F13 OBIA geotransform cache. Skip F7/F12/F14/F18 unless A–D complete with margin (documented).
- [ ] F1 All new tests run twice (`ctest -R`, two consecutive invocations, exit codes recorded).

## Reviews
- [ ] R1 adversarial (independent subagent, Qt/C++ lifetime lens) → findings fixed → re-run oracles ×2.
- [ ] R2 adversarial (fresh lens: GIS workflow user + persistence/perf) → findings fixed → re-run oracles ×2.

## Delivery
- [ ] Ledger files complete; whitelist diff audit; `gh pr create`.
