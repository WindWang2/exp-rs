# Milestones — execution order and commit mapping

| Order | Milestone | Commit subject | Scope |
|-------|-----------|----------------|-------|
| 1 | A — P0 lifecycle | `fix(app): eliminate workbench and canvas lifetime hazards` | inspector_host, selection_context, qgis_display_manager (+vendored canvas settle helper), tests |
| 2 | B — view isolation + async render race | `fix(display): isolate per-view layer state and settle async rendering` | active_view_host, display manager accessor, test_layer_sync_contract/test_active_view_host_data_context |
| 3 | C — concurrency seams | `fix(runtime): harden task and UI concurrency seams` | rs_scan_pool (new), roi/histogram widgets, job_engine transient capacity, task_center pre-registration, data_manager affinity asserts |
| 4 | D — command registry | `refactor(app): complete command registry projections` | shortcut owner set, installShortcut tests, conflict-test expansion |
| 5 | E/F — ContextRules 2.0 + lifecycle unification | `feat(app): unify workbench lifecycle contracts` | ContextRules prerequisite facts, per-bench lifecycle hooks (#813), close policy |
| 6 | G/H — Inspector 2.0 + SchemaFormBuilder 3.0 | `feat(app): expand contextual inspector and schema-driven forms` | #812, lazy sections, SAR/vector sections, form hardening |
| 7 | I/J/K — task UX, IA, a11y | `refactor(app): rationalize workbench information architecture` | unified task states, token/IA normalization, keyboard audits |
| 8 | L — adversarial review | `test(app): expand async rendering and command lifecycle matrix` | 2 subagents; resolution commits |
| 9 | wrap-up | `docs(app): document Professional Workbench 6.0` | docs + FINAL_REPORT |

Each milestone: fix → build → run its test targets → update TEST_MATRIX.md →
commit. FINAL_REPORT.md filled at the end; REVIEW_LOG.md updated after every
review pass.
