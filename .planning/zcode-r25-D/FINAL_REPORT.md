# R25-D FINAL REPORT — Qt/QGIS 遥感工作台交互、显示性能与现有智能编图深化

Branch: `agent/r25-D-qt-qgis-cartography-20261028T170355Z` · Base `2610cb4741` (origin/master at track start) · 13 commits.

## TL;DR

Triage found every candidate swarm-review issue already FIXED in master — so this
track's value came from the W0–W8 depth work itself. The W0 audit surfaced seven
real interaction gaps; seven dev loops closed them with production fixes and
tests, one of which (F8) unblocked the ENTIRE GUI compose path: `cartography:compose`
refused every canvas layer because frame layer refs are seeded from layer SOURCES
while the compiler resolved only workspace keys / project NAME / ID — the W7 E2E
caught it live (RED logged), the compiler gained a source-path resolution tier
(GREEN), and the dock→template→compose→designer→export loop now works end to end.

## Oracle closure (task book §7)

1. **入口/handler 映射**: W0 audit (F1–F7) in LOOP_LEDGER; production fixes F1 (Ribbon
   制图 tab now projects workbench.cartography + compose/check/fix/export), F2
   (cartography.* availability + human reasons via ContextRules::hasMapLayers),
   F4 (one processing-history surface; QGIS LocalProfile modal deleted), F5-adjacent
   (canvas fallback so availability and behavior agree).
2. **三项深化**: (a) pre-run preflight line — the dead estimate seam now derives from
   the authoritative AlgorithmMetaStore sidecars at both tool surfaces (test_r25_d_
   operator_preflight); (b) result→layer linkage — vector artifacts (rs:detect GPKG/
   SHP, polygonize) open from every manual result surface via the kind-inferring
   seam (test_r25_d_result_open_link); (c) cartography chain — compose → hand-tune in
   the layout designer (same QgsLayoutDesignerDialog as project.newLayout) →
   governed export, with honest dock gating (test_r25_d_cartography_dock) AND the
   F8 compiler fix that made the whole chain reachable.
3. **生命周期/异步安全**: full-shell lifecycle 7/7; canvas invalidation discipline
   pinned structurally; no new UAF paths (R1+R2 independent reviews clean).
4. **真实渲染/输出验收**: CP11 render gate runs on this host (5 formerly-gated
   cases execute); 29/32 with the 3 failures IDENTICAL on master-state compiler
   (host baseline, isolated); E2E verifies CRS'd compose→export→project roundtrip.
5. **三类性能证据**: PERF_BASELINE.md — real render suite wall/RSS (6.5 s / 1.44 GB),
   invalidation counts (≤2 for 12 toggles), E2E chain cost (~0.5 s); plus the async
   sampling record. Counts and measurements only, no wall-clock gates.
6. **不重建**: everything extends CommandRegistry / ContextRules / TaskCenter /
   TemplateRegistry / LayoutService / existing docks. C/B APIs consumed read-only.
7. **真实可见可操作**: every UI change is a real widget-state change (gating,
   tooltips, ribbon buttons, preflight label) — verified by driving the REAL dock
   and REAL registry in tests.

## Verification summary

- This track's 6 suites: **11/11 passed ×2** (plus a final ×2 after R2 fixes).
- Neighborhood: test_mapspec 239/2830 ✓, test_qgis_display_manager 25/6490 ✓,
  command_registry/selection_context/palette/shortcut_conflicts ✓,
  test_workbench_full_shell_lifecycle 7/126 ✓.
- Registration integrity: base 1063 targets → HEAD 1069, lost 0, duplicates 0.
- R1 (independent): 5 findings → all closed (4fceb2354c). R2 (fresh context):
  no new defects; 2 P3s taken (1a698e7652).

## Baseline failures isolated (NOT this diff)

- CP11: atlas-series-a4l & report-atlas-appendix-a4l corpus produce ("atlas enabled
  but no coverage_layer"), 1 produce case at :812 + byte-equality case at :1199 —
  identical on master-state compiler; GDAL PNG update-access noise on this host.
- #1638 (vendored src/gui stubs), #1565, #1695 E-zone sub-items → CROSS_TRACK_FINDINGS.

## Blockers / NOT_EXECUTED

- github.com unreachable from this host (TLS eof): FetchContent deps reused from the
  sibling r25-C exact-version cache (catch2 v3.7.1 / pybind11 v2.13.6) — build
  unaffected, noted for reproducibility.
- GPU/TensorRT/OpenVINO paths: NOT_EXECUTED(no hardware) — untouched by this diff.
- Online STAC/network identify: no network on host; async design verified by code
  evidence + existing suites, no live round-trip claimed.
