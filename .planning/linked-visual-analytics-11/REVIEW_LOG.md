# REVIEW_LOG — linked-visual-analytics-11

## Round 1 — independent adversarial review (subagent #2, read-only)

Reviewer: general-purpose agent, full `git diff origin/master...HEAD` +
planning docs; vendored QGIS headers checked for API claims. Verdict
payload: `{"P0": 1, "P1": 3, "P2": 3, "P3": 7}`.

| ID | Sev | Finding | Disposition | Commit |
|---|---|---|---|---|
| P0-1 | P0 | Scatter pick indexed the RAW snapshot (`m_lastScatter`) while the widget shows the FILTERED payload → marker at wrong map location after any brush | **FIXED**: panel now tracks `m_displayedScatter` (set on source ready and on filter); new pure `scatterPickToMapPoint(displayed, index)` resolves picks; regression test pins pick-after-filter known answer (value-2 point → pixel (20,20) → map (20,-20)) | fix commit R1 |
| P1-1 | P1 | Hub dispatch context not restored after nested cross-origin dispatch → outer-origin echo escapes suppression (A↔B relay could recurse); also suppression documented as (origin,generation)-matched but implemented origin-only | **FIXED**: save/restore `prevOrigin/prevGeneration` around emit; docs (hub header) now state origin-scoped suppression; regression test "restores outer dispatch context after nested relay" (relay+echo subscribers, count==2, suppressed==2) | fix commit R1 |
| P1-2 | P1 | Probe inverse assumed north-up affine; rotated raster → silent wrong pixel | **FIXED**: fail-closed guard `gt2 != 0 \|\| gt4 != 0` → typed message, never a wrong value | fix commit R1 |
| P1-3 | P1 | `removeView` left canvas `extentsChanged`/`xyCoordinates` connections installed → remove/re-add cycle double-connects (doubled propagation) | **FIXED**: connections stored in ViewRecord, disconnected in removeView; event-filter install tracked | fix commit R1 |
| P2-1 | P2 | DECISIONS D3 contradicted implementation (reviewer read HEAD before the D3-REVISION doc edit landed in the working tree) | Already resolved: D3-REVISION documents the tree-signal observation as built | earlier commit |
| P2-2 | P2 | `syncVisibilityToPeers` ignored `data::Result<void>` and counted no-ops | **FIXED**: peer no-change short-circuit kept, Result checked (failure ≠ sync), count only on success | fix commit R1 |
| P2-3 | P2 | Hub origin was a class constant → second panel instance shares origin (mutually blind consumption) | **FIXED**: per-instance origin `va.panel#N` (atomic counter) | fix commit R1 |
| P3-1 | P3 | Comment named wrong creation function | FIXED (comment now function-neutral) | fix commit R1 |
| P3-2 | P3 | Layer-controller header claimed QPointer sets | FIXED (raw-pointer destroyed()-pruned sets documented) | fix commit R1 |
| P3-3 | P3 | Dead `VaCursorProbe::Sample` struct + stale signal doc | FIXED (struct removed, signal contract documented) | fix commit R1 |
| P3-4 | P3 | `view.linkUndo` availability with history==1 enables a no-op | FIXED (gate on > 1) | fix commit R1 |
| P3-5 | P3 | `view.linkVisibility` description mentioned opacity toggle it doesn't control | FIXED (description narrowed; opacity stays default-on — documented limitation) | fix commit R1 |
| P3-6 | P3 | `publish()` 0-result ambiguous; probe `requests` undercounted transform-failure path | PARTIAL: probe counter moved to request-acceptance; hub return semantics documented in header (0 = rejected OR echo; distinguish via Stats) — enum return rejected as API churn for zero consumers | fix commit R1 |
| P3-7 | P3 | Loop-oracle partly via own counters; committed test weaker than working-tree revision | DISPOSITION: extent loop-oracle is external (settled extents + appliedSync bound); hub counters are secondary. Strengthened tests committed in fix commit R1 | fix commit R1 |

## Verified non-issues (reviewer, recorded for the record)

- Root-only layer-tree connect is sound (vendored QGIS forwards child
  visibilityChanged to ancestors, qgslayertreenode.cpp:283); mApplying
  absorbs the synchronous write-through echo.
- Probe threading clean: only path + transformed point cross threads;
  RsScanPool stale/cancel mutex-guarded; marshalTo + QPointer = no
  dead-widget callbacks; stats GUI-thread-only.
- Extent/cursor propagation terminates (mApplying absorbs synchronous
  echoes; markers never emit cursor events; undo walk-back is per-call).
- QgsPointXY::isEmpty is a real flag in this vendored tree; mapSettings()
  returns const ref (cheap per-move CRS reads).
- commands.json entries match registered view.* ids.

Post-fix gate: targeted suites re-run after fixes (see TEST_MATRIX.md);
P0=0, P1=0.
