# EVIDENCE — workflow-pipeline-designer (D17)

Policy: every capability claim maps to a local command + exit code, or is
explicitly marked not-executed. No online CI.

## Environment

- Host: Linux 6.18.49-2-lts x64, 16 logical cores / 62 GB RAM.
  Policy: build `-j2` (never `nproc`), tests `-j1`, drop to `-j1` on
  RSS > 70 %. QT_QPA_PLATFORM=offscreen for all UI tests.
- **Toolchain hazard found**: `/home/kevin/.local/bin/cmake` (and `ctest`)
  is a broken Python shim shebanged to the ZCode AppImage — it silently
  launches the desktop app and produces no build output. All D17 commands
  therefore use `/usr/bin/cmake`, `/usr/bin/ctest` and `ninja` directly.
  (Reported for host cleanup; not fixed here — outside repo scope.)
- First configure used the default (Make) generator; reconfigured with
  `-G Ninja` to honor the `-j2` ninja redline.

## Resource Log

Samples during build/test windows (policy triggers: RSS > 70 % or load
> 1.5× cores would force `-j1`; **never triggered**):

- 23:22 load 4.27, mem 32 % (initial configure + warm build)
- 00:09 load 13.23, mem 34 % (peak of the -j2 warm build; other tracks'
  processes also live on the host — D17 held -j2 throughout)
- 01:42 load 9.66, mem 32 % (final verification sweep)

## Commands (chronological)

1. `git fetch origin master` → clean.
2. `git worktree add ../exp-rs-workflow-pipeline-designer -b
   zcode/workflow-pipeline-designer origin/master` → exit 0, HEAD
   `007e70cff6` (BASELINE.md).
3. `.gitignore` whitelist entry added; `git check-ignore -v
   .planning/workflow-pipeline-designer/GOAL.md` → matches
   `!.planning/workflow-pipeline-designer/*.md` (tracked OK).
4. Phase-0 docs committed (`24b68e4c53`).
5. `/usr/bin/cmake --preset dev-default -G Ninja
   -DFETCHCONTENT_SOURCE_DIR_PYBIND11=/home/kevin/projects/rs-studio/main/
   build-dev/_deps/pybind11-src
   -DCMAKE_{C,CXX}_COMPILER_LAUNCHER=/usr/sbin/ccache` → **exit 0**
   ("Generating done").
6. Full builds of the nine D17 targets:
   `ninja -C build-dev test_workflow_ir_v2 test_workflow_dag_analysis
   test_workflow_repair_rules test_workflow_cost_estimator
   test_workflow_agent_tools test_workflow_checkpoint_cache
   test_pipeline_canvas_widget test_guided_workflow_sync
   test_d17_workflow_pipeline_e2e -j2`. Iterations logged in
   /tmp/d17-build*.log; compile errors were fixed per REVIEW_LOG §compile
   fixes; **final build exit 0, 0 errors**.
7. Test execution (serially, `QT_QPA_PLATFORM=offscreen`, from
   `build-dev/tests/`). Final post-review sweep, 2026-09-15T01:42:36+08:00,
   **exit 0 on all nine binaries**:
   - `./tests/test_workflow_ir_v2` — All tests passed (172 assertions in 12 test cases)
   - `./tests/test_workflow_dag_analysis` — All tests passed (71 assertions in 10 test cases)
   - `./tests/test_workflow_repair_rules` — All tests passed (64 assertions in 8 test cases)
   - `./tests/test_workflow_cost_estimator` — All tests passed (42 assertions in 10 test cases)
   - `./tests/test_workflow_agent_tools` — All tests passed (50 assertions in 9 test cases)
   - `./tests/test_workflow_checkpoint_cache` — All tests passed (85 assertions in 8 test cases)
   - `./tests/test_pipeline_canvas_widget` — All tests passed (40 assertions in 7 test cases)
   - `./tests/test_guided_workflow_sync` — All tests passed (41 assertions in 6 test cases)
   - `./tests/test_d17_workflow_pipeline_e2e` — All tests passed (242 assertions in 4 test cases)
   **TOTAL: 807 assertions, 74 test cases, 0 failures.**
8. Dual-axis review (Phase 6): three read-only Explore subagents (the cap,
   no recursion) — Standards axis, Spec/science axis, adversarial
   completion-gate audit. Findings → remediation commits
   `2585aa4904..88c100bcdd`; full ledger in REVIEW_LOG.md.
9. Post-remediation: reconfigure → exit 0; rebuild all nine targets →
   exit 0; full sweep per command 7 → exit 0 everywhere.

## Capability evidence (refs used by CAPABILITY_MATRIX.md)

### C-A — Workflow IR 2.0
- Command: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_workflow_ir_v2`
  → exit 0, "All tests passed (172 assertions in 12 test cases)".
- Covers: S(D(J)) ≡ J byte-identical over the 11 golden fixtures (parse →
  serialize → reparse equality), malformed-document fail-closed errors,
  single-source in-degree rejection, dangling-edge offender naming,
  migrateFromV1 lift + defaults + ghost-wiring rejection, missing-`resolutionX`
  fail-closed pin.

### C-B — DAG analyzer
- Command: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_workflow_dag_analysis`
  → exit 0, "All tests passed (71 assertions in 10 test cases)".
- Covers: diamond hand-truth (T0={S}, T1={A,B}, T2={M}, C_max=2), 3-cycle
  closed path [N1,N2,N3,N1] excluding downstream N4, self-loop, disconnected
  components, 100-node chain, 10×10 layer-cake tier sizes, injected
  back-edge caught fast, cycle-path-is-a-real-walk regression pin.

### C-C — Contract checker + repair engine
- Command: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_workflow_repair_rules`
  → exit 0, "All tests passed (64 assertions in 8 test cases)".
- Covers: CRS violation details (EPSG:4326→EPSG:32649, bilinear), DN→BOA
  calibration+atmosphere chain order, Radiance→TOA single adapter,
  resolution resample targets, wildcard pass-through, compound 4-adapter
  cascade in rule order, collision-free ids, determinism (byte-identical
  healed documents), repair invariant `inspect(apply(infer(W))) == ∅`.

### C-D — Plan optimizer + cost estimator
- Command: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_workflow_cost_estimator`
  → exit 0, "All tests passed (42 assertions in 10 test cases)".
- Covers: two sha256sum-pinned digests, determinism/sensitivity/parent-order
  invariance, twin signatures, DNE=2/CSE=1 (optimized size −3), no-op
  optimize, distinct-port parallel-edge preservation, cache-hit reporting,
  analytic Flops (14e6) and PeakRSS (16e6 + 64 MiB), injected-RAM waterline
  (parallelism 1 under a 64 GiB budget, 2 under 512 GiB).

### C-E — Run coordinator
- Command: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_workflow_checkpoint_cache`
  → exit 0, "All tests passed (85 assertions in 8 test cases)".
- Covers: single-node run + checkpoint document / no-`.tmp` residue, node-5
  failure skip cascade (enumerated 10-state vector), resume with the exact
  CacheHit prefix 1..4, corrupt-checkpoint rejection, cancel to terminal
  states, diamond convergence (merge node executes once), cyclic startRun
  rejection, non-topological-order resume (P0 regression pin).

### C-F — Canvas
- Command: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_pipeline_canvas_widget`
  → exit 0, "All tests passed (40 assertions in 7 test cases)".
- Covers: analytic Bézier midpoint (200,150)±0.5 + short-edge 30 px clamp,
  zoom clamp on both sides, load/export round-trip + moved-position
  readback, 11.9/12.0/12.1 px snap predicate, pending-connection commit +
  cancel, interactive-wiring export survival, 100-node load < 50 ms.

### C-G — Guided workbench
- Command: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_guided_workflow_sync`
  → exit 0, "All tests passed (41 assertions in 6 test cases)".
- Covers: lab02 lift (2 cards, titles, forward/backward manual-step
  attachment, lifted params), single-edit single-emission,
  reentrancy-echo suppression, unknown-node no-op, view-mode flip,
  invalid-LabSpec fail closed, adopt + invalid-document rejection.

### C-H — Agent orchestrator
- Command: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_workflow_agent_tools`
  → exit 0, "All tests passed (50 assertions in 9 test cases)".
- Covers: Draft-07 schema shape, NDVI 4-node chain, water 5-node chain,
  change/fusion/classification intents, unknown/empty goals fail, compile
  determinism (byte-identical documents), CRS-log heal (+1 reproject
  adapter, rule id recorded, contract-clean result), matched-but-unchanged
  typed failure, unknown log typed no-op.

### C-I — E2E
- Command: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_d17_workflow_pipeline_e2e`
  → exit 0, "All tests passed (242 assertions in 4 test cases)".
- Covers: mini E2E through IR→analyze→optimize→execute (artifacts exist),
  crash consistency (abort at 5/10 → resume → exactly 5 CacheHits, all
  Succeeded), 100-node/10-tier run all Succeeded under the 1.5 GiB
  getrusage peak budget, all 11 shipped labs green (16 operator nodes,
  corpus aggregate pin) with scratch cleanup.

## Not executed (explicit)

- ASan/UBSan lane (`sanitizer-debug` preset): not run — that lane builds
  the full qgis chain (hours at `-j2`); the D17 thread/ownership surfaces
  were manually reviewed instead (see REVIEW_LOG verified-safe entries).
  Recommended for the merge lane.
- Windows checkpoint-writer branch (`_wopen/_commit/MoveFileExW`): compiles
  via #ifdef but was never executed on this Linux host.
