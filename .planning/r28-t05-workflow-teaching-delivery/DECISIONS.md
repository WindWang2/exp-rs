# DECISIONS — R28-T05

| # | Decision | Rationale |
|---|---|---|
| D1 | Build profile: Debug + ENABLE_TESTS=ON + ccache launcher + OpenCV_DIR=local opencv5 prefix | Matches main-worktree known-good profile (Debug) so ccache hits maximize; OpenCV enabled because #1812 declared snapshot regeneration blocked on "a real OpenCV toolchain" and the local prefix exists. Divergence risk: OpenCV-gated operators alter registry-driven snapshots — that is exactly the regeneration path WP02 must exercise. |
| D2 | Build dir: `build-r28-t05` inside the track worktree | Mirrors T04's `build-r28-t04` convention; per-worktree isolation; guard-locked. |
| D3 | JOBS=2 everywhere (never >4) | Host shared by 5 tracks; guard + host lock serialize; contract hard cap 4 with escalation rules only after reading build.sh (which itself caps at 2 via SICNU_BUILD_JOBS_CAP). |
| D4 | WE-26 (src/planner/planning_context.cpp) — T03-owned dir, but the issue is workflow-experiment (mine) | Surgical parse-side reject (negative/non-finite resolution_m) with in-code issue reference; recorded as a cross-track interface note in the PR + HANDOFF.md so T03 can object/absorb. |
| D5 | WE-24/25 metric semantics: represent undefined as explicit non-finite-guarded status in the metric JSON (e.g. mape defined only when mapeCount>0, emitted as undefined marker + count), NOT as a new schema version | Consumers already read `mape_skipped_zeros`; extend with explicit `mape_defined` boolean is additive and backward-compatible. Confusion matrix: sparse map representation under a declared memory budget instead of lowering the label cap. |
| D6 | Baseline measurement runs on THIS worktree's build of BASE_SHA before any modification | The build compiles the exact master tree; test-run happens pre-change (WP01). |

| D7 | WP11 sleeps: test_workflow_checkpoint_cache.cpp's 7 `msleep` sites stay | Each is a deliberate cancel/scheduling window with an inline purpose comment ("let the hash get deep into the file"), not sleep-then-assert. #1751's actionable class is sleeps followed by direct assertions; none of these 7 match. Converting them to content waits would change the interleaving under test. Revisit only if the baseline run flakes here. |

| D8 | R1 review fixes (P1-1/2/3, P2-4/5/6/7, P3-12) applied in one batch; P3-8 (whitespace ids, pre-existing laxity), P3-9 (empty-matrix increment guard — no caller path constructs one), P3-10 (toJson matrixSums hoist, perf-only), P3-11 (stale .importing sweep — unsafe under concurrent imports) deferred with notes | Contract requires P0/P1 + reproducible P2 fixed with regressions; deferred items carry impact + rationale for R2 |
| D9 | P3-13 correction: WE-24's undefined-MAPE emission (number → JSON null + mape_defined) is a consumer-visible semantic change, not purely additive — grader metric criteria over an undefined mape now read as missing/Indeterminate instead of silently comparing 0.0, which is the intended honest behavior | D5's "additive" claim amended |
