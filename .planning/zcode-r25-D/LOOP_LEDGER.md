# R25-D Loop Ledger — every loop: change → verification → evidence

Format: `#N | W# | change | verification | verdict | next`

## Pre-loop recon (W0)

- Recon: master=2610cb4741 (unchanged), 0 open PRs, 8 leftover branches all ahead=0, 5 local r25 worktrees (A/B/C/E siblings).
- Issue triage: 15 candidates verified → **ALL in-zone candidates FIXED_IN_MASTER_BUT_OPEN** (#1535 #1536 #1537 #1559 #1560 #1561 #1562 #1563 #1564 #1565-out-of-zone #1632(6/6) #1655(gate inverted to run-by-default) #1695(AsyncRunner items)); #1638 = vendored src/gui (out of zone, cross-track note).
- Consequence: this track's value = the W0-W8 depth work itself (audit → real gaps → production fixes + tests + evidence), NOT re-fixing fixed issues.
- Baseline build queued on shared flock `~/.cache/exp-rs-build.lock` (script `.planning/zcode-r25-D/build_baseline.sh`, build-ci-fast/ inside this worktree).

## W0 entry/handler audit (Explore agent, read-only) — key real findings

F1. **Cartography ribbon tab lacks the CartographyDock entry** — ribbon_controller.cpp:1179-1182 only wires Print Layout/Swipe; the dock is reachable only via 窗口 menu + command palette (`workbench.cartography`, command_defs.cpp:319-326). Discoverability gap on the primary cartography surface.
F2. **`cartography.compose/preflight/repair/export` commands: no availability rules, always enabled** (command_defs.cpp:344-395) — fake availability; errors surface only via statusBar after the click (no template / no layers / no composed layout / no directory: cartography_dock.cpp:235,242,474-477,527-530).
F3. **TaskPanelHost `m_estimate` ("Preflight/resource estimate line") is dead** — task_panel_host.h:52, setEstimate task_panel_host.cpp:165-174, zero callers repo-wide. W2 wants a real pre-run preflight/guidance chain.
F4. **Processing History dual path**: Ribbon Tasks tab + 处理 menu → `showProcessingHistory` (modal legacy dialog, main_window_misc.cpp:131) vs command `workbench.processingHistory` → `showUnifiedProcessingHistory` (workbench panel, main_window_workbench.cpp:965). Same label, two panels — violates the one-handler law (ui-architecture §1).
F5. Ribbon direct-connect buttons without context gating: 8 `openWorkflowTool` buttons always enabled and silently no-op without a workflow controller (ribbon_controller.cpp:1058-1158, main_window_docks.cpp:1168-1170); Pipeline Stop Run clickable when idle (:1271-1279); Undo/Redo clickable on empty stacks (:847-852).
F6. Shell language is uniformly ENGLISH tr() strings (CartographyDock consistent with the rest) — no i18n divergence to fix there.
F7. Run-button gating in TaskPanelHost is honest (validationBlocked + tooltip reasons); auto-load chain (task_center.cpp:3232→layerAutoLoadRequested→main_window_docks.cpp:686-721 epoch-guarded) and governance refresh (WorkspaceService::entityChanged → 300ms coalesced) are sound.

## Loops

#1 | W0 | ledger files created | n/a | PASS | next: baseline build (queued)

#1 | W0/W4 | cartography.* availability (ContextRules::hasMapLayers + requirementFacts family), ribbon 制图 tab registry projections (workbench + compose/check/fix/export), dock honest gating + public adoptSpec/refreshActionStates | tests written (test_r25_d_cartography_ux pure+scan, test_r25_d_cartography_dock functional); run pending build | CODE GREEN expected (RED provable statically: master's requirementFacts has no cartography case → facts empty ≠ 1) | commit 6a1ee37a87
#2 | W2 | operatorPreflightSummary (AlgorithmMetaStore sidecars, #707 GPU rule, truncateSummary bound) + wiring at both showTool sites | test_r25_d_operator_preflight (real sidecars + wiring scan); run pending build | RED static: master has no setEstimate callers → scan fails | commit 7818a5fe19
#3 | W2 | processing-history single surface: menu+ribbon → workbench.processingHistory; legacy QGIS LocalProfile modal + slot deleted | grep-verified zero remaining refs; no dedicated test (behavior = command projection covered by registry suites) | DONE | commit cb324216fd
#4 | W3 | canvas invalidation coalescing pin (counts: ≤2 layer-set changes for N toggles, exactly 1 batch sync) | run pending build; may PASS on master by design (pin, not defect) | pin | commit 8103ee11f9
#5 | W4 | designer hand-off: dock 'Open in Designer…' + openLayoutDesignerRequested → shell opens the SAME QgsLayoutDesignerDialog; canvas fallback in dock layer provider | test_r25_d_cartography_dock designer cases (real LayoutService roundtrip); run pending build | RED static: no designer button exists on master | commit 99d6e3052c
#6 | W1/W2 | result-open kind linkage: 4 manual surfaces route via loadDataLayer (vector artifacts openable) | test_r25_d_result_open_link source pin; run pending build | RED static: master binds loadRasterLayer | commit d3e228a9db
#7 | W7 | E2E display/cartography contract (asset→view→TaskCenter rs:ndvi→result layer→compose→export→project roundtrip) | run pending build | new coverage | commit f7a7700a1e

## R1 (independent review, agent_b5a78888, read-only diff review + real compile probes)

Verdict: 2×P0 compile + 2×P1 runtime/data + 1×P1 protocol. All closed in commit 4fceb2354c:
- R1-1 P0 main_window.h lost showProcessingToolbox() → restored (my removal regex ate two lines)
- R1-2 P0 ContextRules using-directive on namespace → namespace alias
- R1-3 P0 preflight test TU-embed link failure → link sicnu_processing
- R1-4 P0 E2E incomplete QgsLayoutManager → include added
- R1-5 P1 whitespace-sensitive source-pin regexes → raw-string tolerant regexes
- R1-6 P1 canvas-invalidation sync assumption → real semantics (bridge carries visibility; manager count unchanged)
- R1-7 P1 preflight line invisible in GUI sessions → process-wide lazy loadDefaults in operatorPreflightSummary
- R1-8 P1 114 foreign tests/CMakeLists.txt lines rewritten by stray sed → file rebuilt master+append-only (0 deletions); integrity script: 1063→1069 targets, 0 lost
- R1-9..11 P3 noted: QObject::tr context (kept, consistent with command_defs), temporal preview still loadRasterLayer (defensible, noted), double blank line (fixed with R1-1)
Also verified clean: command/ribbon registration ORDER (registry before ribbon build), dock busy ordering, designer lambda parity with newLayout, four loadDataLayer error-path parity, E2E isolation, boundary compliance (src/app only).
