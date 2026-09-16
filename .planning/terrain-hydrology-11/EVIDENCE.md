# EVIDENCE — terrain-hydrology-11

Append-only. Every capability claim maps to a command + exit code run in this
worktree, or is marked not-executed with the blocker.

## Phase 0 (2026-09-15)

- `git fetch origin --prune` → exit 0; origin/master = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`.
- `gh pr list` → 1 open PR (#1008 spectral/radiometric; CONFLICTING vs master).
  `gh pr view 1008` + `gh pr diff 1008 --name-only` → no terrain file overlap
  (see PARALLEL_OWNERSHIP.md).
- `gh issue list` → #1001–#1007, all io/workflow/dataset/georef → OUT_OF_SCOPE for
  terrain track; no dedupe against any planned deliverable.
- ISSUES.md read (lines 1–60): stale D3 operator-gap backlog (temporal/SAR/HSI/
  cartography); none terrain → not implemented.
- Code audit citations: see BASELINE.md (terrain_flow.h contracts;
  rs_terrain_flow_operator.cpp:70 documented flat-resolution debt).
- `git worktree add ../exp-rs-terrain-hydrology-11 -b zcode/terrain-hydrology-11 origin/master` → exit 0.
- `git check-ignore -v .planning/terrain-hydrology-11/GOAL.md` → matched
  `.gitignore:119:.planning/*` before fix; after whitelist append
  (`!.planning/terrain-hydrology-11/**`) the path is no longer ignored
  (verified via `git status` visibility of the planning files).
- OUT_OF_SCOPE findings: none yet.

## Phases 1–4 (2026-09-15/16)

- Cold build exit 0 (see PERFORMANCE.md for command/caps). All 7 terrain suites green
  (direct binary runs, `QT_QPA_PLATFORM=offscreen`):
  - test_terrain_hydrology: 12 cases / 1023 assertions PASS
  - test_terrain_viewshed: 6 cases / 938 assertions PASS
  - test_terrain_solar: 5 cases / 62 assertions PASS
  - test_terrain_landform: 5 cases / 198 assertions PASS
  - test_terrain_analytics_e2e: 9 cases / 1073 assertions PASS (registry E2E,
    cancellation, cell-budget guard)
  - test_terrain_agent_tools: 3 cases / 52 assertions PASS (Unicode paths included)
  - test_terrain_foundation5 (legacy regression): 13 cases / 267 assertions PASS
  - test_terrain (legacy regression): PASS (via ctest, 14-case terrain batch 100%)
- Kernel-level scale evidence: opt-in hermetic scale test (2048², one-exit funnel)
  PASS in 6.99 s; D8 acc(0,0)=N and D∞ downstream-monotonicity invariant hold.
  Default gate unchanged (scale test SKIPs without SICNU_TERRAIN_SCALE_TESTS=ON).
- Capability integration: `capability_knowledge_tool gen-meta` exit 0
  ("wrote 135 capability sidecars"), `gen-pages` exit 0 ("wrote pages, 0");
  test_capability_knowledge: 12 cases / 1210 assertions ALL PASS.
- test_layout_tools regression (dialog/agent-adjacent): 4/4 PASS via ctest.
- Debugging evidence (probes under /tmp, kernel fixes they produced):
  1. D∞ wedge-membership tolerance (exact-boundary planes rejected on 1-ulp α<0) —
     probe: plane z=−col → 90° everywhere after fix.
  2. Pit rule + D8 fallback (cone apex/radial-wall degenerate facet geometry) —
     probe: pit centre acc = 121 = N_valid; plane acc(col)=w−col exact.
  3. shadowDuration ray-start enumeration (half-step predecessor test missed
     boundary cells) — replaced with analytic upstream-edge starts; wall shadow
     falls WEST of an east-lit wall, exactly per parallel-ray model.
  4. horizonProfile/geomorphon centre-to-centre angle distance (nearest-cell t
     overestimated diagonal angles by up to atan(2√2)−atan(2)).
  5. tpi_multiscale operator: single output creation (abandon+recreate same path
     raced GTiff create).
- OUT_OF_SCOPE integration repairs (master pre-existing, control-run proven):
  - sicnu_agent did not compile on master (`data_platform_tools.cpp` unqualified
    `BenchmarkService` etc. since the D19 merge) — fixed with one
    `using namespace sicnu::experiment;` directive; verified against master source.
  - capability coverage gap: 7 operators (spectral/temporal domains, merged by
    earlier tracks) had no sidecars — authored via the official
    capability_knowledge_tool derivation with honest minimal enrichment.
  - data/agent/capabilities/preprocess.json carried duplicate rs:gaofen/zy3/hj_import
    entries (dup of io.json, #956-era) blocking `gen-meta` — removed the stale
    preprocess-family copies (io.json is the authoritative projection).
  - control run (my sidecars removed, flow sidecar stashed): capability suite had 4
    failures; with my changes: 0 failures. Net: no regression, 4 pre-existing
    failures fixed.
  - Remaining pre-existing-on-master test noise NOT touched: Catch2 discovery
    `*_NOT_BUILT` placeholders (sicnu_test_main does not implement
    `--list-test-names-only`; repo-wide, affects ctest -R by target name only).
