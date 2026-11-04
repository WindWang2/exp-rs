# R27-T04 Issue Triage (re-verified against master 10652fd0, 2026-11-02)

All 14 candidate issues re-checked at BASE_SHA with code evidence, not issue text.

| Issue | Pri | Real-time state at HEAD | Triage decision |
|---|---|---|---|
| #1745 ROI spectrum re-invoke double-free/UAF (F1) | P1 | OPEN; reproducible code path (`unsetMapTool`→`deactivate`→`deleteLater` + sync `delete`) | **FIX in T4** |
| #1746 orphan classId heap OOB write (F8) | P1 | OPEN; `bandSums[classId][bi] +=` on QHash-inserted empty vector; orphans via rebuild/merge paths | **FIX in T4** |
| #1747 downgrade refusal still overwrites project, SICNU block lost (F15) | P1 | OPEN; serializer failure cannot abort `QgsProject::write` | **FIX in T4** |
| #1788 STAC "More Results" replaces instead of appends (F2) | P2 | OPEN; `populateResults` resets table | **FIX in T4** |
| #1789 searchNext no-op strands More button (F3) | P2 | OPEN; no signal on no-op; error paths leave stale More | **FIX in T4** |
| #1790 cross-section sync GDAL on GUI thread (F4) | P2 | OPEN; `extractProfile` sync open+sample loop | **FIX in T4** |
| #1791 magic wand sync GDAL + fill on GUI thread (F5) | P2 | OPEN; windowed read+flood fill in canvasReleaseEvent | **FIX in T4** |
| #1792 export remove-before-copy, dest==src kills source (F9) | P2 | OPEN | **FIX in T4** |
| #1793 pipeline JSON no stackLimit/try-catch → abort (F10) | P2 | OPEN; house pattern exists (lab_cockpit_dock.cpp:326) | **FIX in T4** |
| #1794 classification full-scanline re-read per ROI change on GUI thread (F11) | P2 | OPEN; recompute is sync in GUI slot | **FIX in T4** |
| #1795 sidecar deleted before both channels persisted (F16) | P2 | OPEN | **FIX in T4** |
| #1561 spectral profile sync GDALOpen on network source | P2 | **FIXED_IN_MASTER**: `923595d0e2` added `isNetworkSource`/`startRemoteSampling` (worker sampling for /vsicurl/ etc.), still present at HEAD | **No re-implementation**; add regression guard only if trivially testable |
| #1765 STAC COG raw /vsicurl/ bypasses SSRF (RIO-4) | P2 | OPEN; GUI entry never engages `engageProcessPrivateNetworkBlocking()` (only CLI/worker do) | **FIX at T4-owned call site**: engage process network policy in GUI entry; no `src/geospatial/**` edits |
| #1805 P3 summary (F6-F18) | P3 | OPEN checklist | **Capacity-gated**: pick low-risk items with real evidence (see TASKS) — F17 NaN guard, F6 CGImageRelease, F13 geotransform cache, F7/F18 if time; NO mock work |

## Out of scope / redirected
- `src/geospatial/stac/stac_client.cpp` (canonical client) and `src/geospatial/remote/**` — T2 territory; read-only.
- `src/app/agent_ops/**` — T3; not touched.
- R25/R26 merged features (PR #1724-#1731): verified not to duplicate — none of the 13 fix items above appear in those PRs' scope (they covered canvas invalidation discipline, classification UX honesty, guided completion gates, etc.).
