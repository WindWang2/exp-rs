# R25-D GOAL — Qt/QGIS 遥感工作台交互、显示性能与现有智能编图深化

One-line: deepen the EXISTING workbench (no new product direction, no parallel
frameworks) along interaction correctness, display performance, and the
existing intelligent cartography chain, ending in a real PR.

## Hard oracles (from task book §7)

1. 全工作台入口/handler/enablement 映射 — fake-availability or dual-path findings get production fixes; feature/help/status consistent.
2. ≥3 项现有工作台/制图体验深化, each wired to real production handlers:
   - a pre-run preflight/guidance chain improvement,
   - a Layer/Result/Task status linkage improvement,
   - a real MapSpec/chart/export enhancement.
3. UI lifecycle & async safety: project close, task cancel, layer destroy, dock rebuild, view switch — no reproducible UAF/deadlock/stale selection; testable probes.
4. QGIS real render/map-output acceptance: CRS, extent, MapSpec, legend, DPI, export, project roundtrip — runnable host must not rely on permanent skips (#1655).
5. ≥3 classes of comparable visual/interaction performance evidence (render invalidations / frame time / GUI stall or RSS), no fabricated FPS.
6. No rebuild of DataManager/ChartRegistry/TaskCenter/Pi; correct C/B API usage inside write zone; shared requests are handoffs, never races.
7. UI features really visible/operable/recoverable — QSS/mockup-only changes don't count.

## Work packages

- W0 recon+ledger (this dir), baseline build/test
- W1 data/layer dual-plane correctness review (QgisDisplayManager/ActiveViewHost/ProjectContext/DataManagerPanel/SelectionContext)
- W2 ribbon/task panel/schema form unification review + #1632-1/2/4 fixes
- W3 display performance: #1561 spectral async sampling, #1655 CP11 render gate honesty, GUI-stall probes
- W4 MapSpec/cartography deepening (templates/legend/scale bar/north arrow/preflight/export)
- W5 chart↔map linking & view state stability (+ #1632-5)
- W6 teaching-user usability/accessibility (+ #1632-6)
- W7 end-to-end display/cartography contract test (import→display→tool→result→layer→mapspec→export→restore)
- W8 R1/R2 independent reviews + regression + PR

## Non-goals (hard)

- No new UI framework, no whole-UI rewrite, no second chart registry/task system/data manager.
- No writes outside the D-zone (see task book §2). `src/app/{teaching,teaching_admin,experiment_studio}` = E. `src/analysis`, `src/operators/rs`, science algos = A. `src/{data,geospatial,dataset,experiment}`, model runtime = B. `src/agent` non-cartography, `src/jobs`, `src/workflow`, `pi/`, `src/plugins` = C. Vendored `src/core`, `src/gui` prefer zero-touch.
- No fabricated tests/reviews/PR; no waiting for CI; no auto-merge.
