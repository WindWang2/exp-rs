# R25-D ORACLES (task book §7) → delivery mapping

1. **入口/handler/enablement 映射 + 生产修复** — W0 audit (F1–F7) in LOOP_LEDGER; production fixes: cartography.* availability + reasons (L1), single history surface (L3), canvas fallback consistency (L5). Evidence: test_r25_d_cartography_ux, test_r25_d_result_open_link.
2. **≥3 深化** — (a) 预检链: sidecar-derived preflight line (L2, test_r25_d_operator_preflight); (b) 状态联动: result artifacts open on the map regardless of kind — vector results were unloadable from every manual surface (L6, test_r25_d_result_open_link); (c) MapSpec/编图增强: compose → hand-tune in layout designer → export chain + honest dock gating (L1+L5, test_r25_d_cartography_dock).
3. **生命周期/异步安全** — pinned probes: canvas invalidation discipline (L4); teardown paths covered by existing suites re-run (test_canvas_teardown_r4, test_qgis_display_manager); no new UAF introduced (R1/R2 review).
4. **真实渲染/输出验收** — E2E through real TaskCenter + real compose/export + project roundtrip (W7 test); CP11 render-gated cases run on this host (gate is run-by-default in master; #1655 verified FIXED).
5. **≥3 类性能证据** — invalidation counts (L4 structural pin), CP11 render/export measurements + RSS (PERF_BASELINE.md, /usr/bin/time -v), GUI-thread async sampling design record (#1561 fixed-in-master, documented).
6. **不重建既有服务** — all work extends CommandRegistry/TaskCenter/TemplateRegistry/LayoutService/dock seams; no second registry/renderer/task system. C/B APIs consumed read-only.
7. **真实可见可操作** — every UI change is a real widget state change with tooltips; dock test drives the REAL dock.
