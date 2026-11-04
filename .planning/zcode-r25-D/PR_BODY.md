# r25-D: Qt/QGIS 遥感工作台交互、显示性能与现有智能编图深化

Track R25-D · `.planning/zcode-r25-D/` holds the full evidence ledger (BASELINE / ISSUE_TRIAGE / ORACLES / LOOP_LEDGER / TEST_RESULTS / PERF_BASELINE / REVIEW_R1 / REVIEW_R2 / FINAL_REPORT).

- **BASE**: `2610cb4741e69a89124921e44c03e340e2522ee2` (origin/master at track start; 0 open PRs then)
- **HEAD**: see PR commits · **Worktree/branch**: `agent/r25-D-qt-qgis-cartography-20261028T170355Z`

## What this PR does

W0 entry/handler audit found seven real interaction gaps in the existing workbench; seven small loops closed them inside the D-zone (src/app shell + src/agent/{mapspec,cartography}) — no new frameworks, no parallel services:

1. **Cartography surface honesty** — `cartography.compose/preflight/repair/export` gain a `ContextRules::hasMapLayers` availability family (human reasons follow the requirement-facts derivation); the Ribbon 制图 tab projects `workbench.cartography` + the compose→check→fix→export chain through the CommandRegistry (the Cartography Workbench was unreachable from its own tab); the dock gates its buttons honestly (busy > templates > layers > composed-layout for Export) with tooltips that clear when blockers do; `adoptSpec` is the public workflow/agent handoff seam.
2. **Pre-run preflight line (dead seam revived)** — `TaskPanelHost::setEstimate` had zero callers; `operatorPreflightSummary` now derives the line from the authoritative AlgorithmMetaStore sidecars (task family, input→output, GPU wording under the #707 declared-only rule, bounded notes) at both tool surfaces (openTool + selectStep), with a process-wide lazy load so pure GUI sessions see it.
3. **One processing-history surface** — the 处理 menu + Tasks ribbon opened QGIS's LocalProfile history dialog (a registry this product never writes to); both now project `workbench.processingHistory` (unified TaskCenter/workflow panel) and the dead modal is deleted.
4. **Result→layer linkage for ALL artifact kinds** — every manual result surface (job panel 结果 tab, task panel, governance rows, history) bound `loadRasterLayer`; vector artifacts (rs:detect GPKG/SHP, polygonize) now open through the kind-inferring seam the automatic load path already uses.
5. **compose → hand-tune in the layout designer → export** — new dock action emits `openLayoutDesignerRequested`; the shell opens the SAME vendored QgsLayoutDesignerDialog `project.newLayout` uses. Plus the dock's layer provider gains the promised canvas fallback (an unselected canvas used to draft nothing while availability said Compose was ready).
6. **MapSpec compose resolves frame layer refs by SOURCE path (real defect fix)** — the compiler resolved refs only via workspace keys / project NAME / ID, so the dock's canvas-source refs were ALL refused ("unresolvable reference(s)"): the GUI compose path was broken end to end. A source-path tier (exact, then canonical) closes the loop for the dock, agent tools and workflow presets alike. Caught live by the new W7 E2E (RED logged in LOOP_LEDGER #7 → GREEN).
7. **Canvas invalidation discipline pin** — structural counts (12 visibility toggles ⇒ ≤2 canvas layer-set changes, 0 manager syncs) guard the bridge/batch coalescing against render-storm regressions.

## Issue accounting (all verified against live master — none re-fixed blindly)

- FIXED_IN_MASTER_BUT_OPEN (verified, no action): #1535 #1536 #1537 #1559 #1560 #1561 #1562 #1563 #1564 #1632 (all six) #1655 (gate inverted to run-by-default) #1695 (AsyncRunner items).
- Out of zone / cross-track (CROSS_TRACK_FINDINGS.md): #1638 (vendored src/gui), #1565 + #1695 E-zone sub-items (E track).

## Tests (all local, offscreen, CTEST_PARALLEL_LEVEL=1)

- New suites (6 targets, registered append-only under `# BEGIN R25-D/# END R25-D`): **11/11 ×2 GREEN**, re-verified ×2 after review fixes.
- E2E (`test_r25_d_e2e_display_cartography`): synthetic EPSG:4326 4-band product → Data asset → real display view (project tree/store, main-view shape) → **real TaskCenter job rs:ndvi** → result layer joins the same view → template draft → **cartography:compose** → **governed export (PNG, non-empty artifact)** → QgsProject save/clear/load keeps the layout.
- Neighborhood regression all green: test_mapspec **239 cases/2830 assertions** (covers the compiler change), test_qgis_display_manager 25/6490, command_registry 11/57, selection_context 15/72, command_palette 4/23, shortcut_conflicts 4/28, **test_workbench_full_shell_lifecycle 7/126** (real window offscreen).
- Registration integrity: `check_merge_registration_integrity.py` → 1063→1069 targets, **lost 0**, duplicates 0; tests/CMakeLists.txt is strictly append-only vs master (0 deleted lines).
- CP11 render suite (gate ACTIVE, #1655 contract): 29/32 on this host; the 3 failures are **identical with the master-state compiler** (verified by rebuild+rerun) — host/template baseline, isolated in TEST_RESULTS.md.

## Performance evidence (PERF_BASELINE.md — measurements, never gates)

Real render/export suite: 6.5 s wall / 1.44 GB peak RSS (32 cases incl. 56-template corpus). Invalidation counts: ≤2 layer-set changes for 12 toggles. E2E chain (TaskCenter + compose + export + roundtrip): ≈0.5 s. Async-by-design records for network identify + dock-off-GUI-thread actions documented.

## Reviews

- **R1** (independent subagent, real compile probes): 2×P0 compile + 2×P1 runtime + 1×P1 protocol → all closed in 4fceb2354c (incl. restoring `showProcessingToolbox()` my removal regex had eaten, and rebuilding tests/CMakeLists.txt as master+append-only after a stray sed).
- **R2** (fresh-context subagent): **no new defects; mergeable**; 2 P3 polish items taken in 1a698e7652 (`QFileInfo::canonicalFilePath`, loader-semantics note). Merge-surface vs sibling r25 branches verified to be only .gitignore + tests/CMakeLists.txt (append-safe).

## Environment notes / NOT_EXECUTED

- Shared-host discipline: flock build lock, `-j2`, ccache; targeted builds for iteration per protocol.
- github.com unreachable from this host (TLS eof) → FetchContent deps reused from the sibling r25-C exact-version cache (catch2 v3.7.1, pybind11 v2.13.6); zero network needed for tests.
- NOT_EXECUTED: GPU/TensorRT/OpenVINO (no hardware), live online STAC round-trips (no network) — nothing in this diff depends on them.
- Per protocol: PR stops here — no CI wait, no auto-merge.
