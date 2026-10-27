# RISK_REGISTER — R24-C

| Risk | L/M | Mitigation |
|------|-----|------------|
| Fresh-worktree full build too slow (vendored QGIS src/core+src/gui) on -j2 | M | Build only needed targets incrementally (sicnu_agent + specific test targets); reuse warm vcpkg binary cache; if a needed test target drags huge deps, prefer lighter adjacent coverage and mark NOT_EXECUTED honestly. |
| tests/CMakeLists.txt merge loss (repeat of #1516/#1714→#1715) | M | Single append-only R24-C block; run scripts/check_merge_registration_integrity.py --require-refs pre-push; never reformat sibling blocks. |
| Modifying pre-existing tests (L2/L3) conflicts with sibling tracks | L | 0 open PRs at baseline; re-verify file unchanged vs origin/master immediately before edit; document exact hunks. |
| Layout designer menu removal breaks ported QGIS interface expectations | M | Only remove empty menu WIDGETS from menus, not the interface API; run test_layout_designer* families. |
| teaching_admin off-thread change risks regressions in grading flow (adjacent read-only backend) | M | D-10 downgrade path: keep behavioral surface; if review flags risk, convert to CROSS_TRACK_FINDINGS with repro instead of forcing change. |
| Workspace-state schema addition consumed by consumers expecting empty charts | L | charts key already exists in contract (test asserts member); only content changes from [] to populated — strictly additive; surface snapshot regen (tests/surface_diff_snapshot.json) must NOT change (charts is workspace-state, not tool surface) — verify test_surface_snapshot stays GREEN. |
| Qt offscreen limits (no real GPU/HiDPI assertions on this host) | M | Mark UNTESTED-ON specifics in FINAL_REPORT; rely on offscreen smoke + structural tests. |
| Concurrent sibling tracks building simultaneously (RAM pressure) | M | Stay at -j2 (cap 4 never used unless ≥12 GiB free AND no other ninja running); check `tasklist` for ninja/cl before heavy links. |
