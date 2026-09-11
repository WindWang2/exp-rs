# FINAL REPORT — Unified Contract / Help / Verification / Portability / Release Platform 9.0

> Status: IN PROGRESS — updated at each milestone completion.
> Branch: `feat/unified-contract-verification-9` (worktree
> `/home/kevin/projects/rs-studio/wt-contract-verification-9`)

## Baseline

- `origin/master` = `f316dfdbb4` ("fix(issues): resolve all 30 P1/P2 issues
  (#853-#882)"); zero open issues; five parallel `-9` PRs open (#883-#887)
  — footprint map in OVERLAP_MAP.md.
- Direct predecessor: Verification Platform 8.0 (PR #839) — ladder,
  readiness, trace/fault/portability infra reused, not rebuilt.
- Historical drift issues #869/#870/#871/#872/#879/#880/#881/#882 re-triaged
  on latest master: all closed by f316dfdbb4, but fixed *by hand* with no
  mechanical guard — the root cause class (implementation ↔ projection
  drift) remained unguarded. See ISSUE_TRIAGE.md.
- Baseline repair: master did not compile on this host (GDAL 3.13 changed
  `GDALMDArrayRead`'s `count` parameter from `GUInt64*` to `size_t*`);
  one-line fix in `src/geospatial/metadata/canonical_metadata.cpp`
  (semantics unchanged; noted for the open #887 which owns that tree).

## Delivered (behavior changes, exact)

New product-code-free contract library (tooling/test side only):

1. `src/contracts/` — text_scan, operator_param_scanner,
   contract_descriptor, contract_graph, command_ref_scanner,
   error_code_scanner, graph_assembly, snapshot tool `contract_inventory`.
2. `data/contracts/contract_graph.snap.json` — committed generated snapshot.
3. `tests/test_contract_platform_9`, `test_contract_projection_9`,
   `test_command_contract_9`, `test_diagnostics_contract_9`,
   `test_capability_contract_9`, `benchmark_contract9`.
4. `scripts/verification_ladder.py` — five contract-9 lanes in L2 +
   `benchmark_contract9` in L7 (additive).
5. `scripts/collect_readiness.py` — contract capabilities registered
   (additive).
6. `docs/verification/CONTRACT_PLATFORM_9.md`.

## Evidence (local, reproducible; online CI/CD NOT awaited)

- Build: Release + Ninja + GCC, worktree build dir; two baseline compile
  repairs were required (GDAL 3.13 size_t; experiment run_bridge const) and
  are committed separately.
- Contract suites (this branch, all green):
  - test_contract_platform_9     — 39 assertions / 4 cases
  - test_contract_projection_9   — 2316 assertions / 9 cases (incl. mutation)
  - test_command_contract_9      — 147 assertions / 6 cases
  - test_diagnostics_contract_9  — 155 assertions / 6 cases
  - test_capability_contract_9   — 1771 assertions / 5 cases
- Regression (existing suites touched by the product edits): test_help_coverage
  (8196), test_algorithm_meta_drift (2270), test_algorithm_schema (31),
  test_cli_commands_json (33), test_io_operators — all pass.
  test_capability_drift fails on master identically (pre-existing: 4
  scientific-processing-8 operators lack capability entries — owned by the
  harness track); unchanged by this branch.
- Benchmark (Release, linux, 16 cores): operator_param_scan 1511 ms
  (136 operators), descriptor_projection 7.3 ms (936 params), graph_assembly
  1816 ms (824 nodes / 263 edges), graph_serialize 4.8 ms (218 KiB) —
  benchmarks/contract9.json, schema exp.bench.contract9.v1.
- Snapshot: data/contracts/contract_graph.snap.json — 824 nodes / 263 edges,
  byte-verified by test_contract_platform_9; regenerable via
  `contract_inventory --source-root . --out data/contracts/…`.

## Adversarial review

See REVIEW_LOG.md (self-review round 1 + reviewer dispositions).

## Known limitations / follow-ups

- Determinism grade stamping (ADR 0124) is partial on master (8/136 schema
  surfaces stamp it). The guard enforces value validity + monotone coverage;
  full adoption needs the operator-owner tracks.
- 25 of 136 operators use idioms the scanner deliberately does not guess
  (OTB application wrappers, OpenCV template-method, macro-generated
  raster-spatial family, inherited fusion aliases); each is allow-listed
  with a reason and the projection guard skips unreliable directions for
  them. Extending coverage further is a follow-up.
- Preflight suggested actions carry 12 stale ids (dot-vs-colon tool
  separators and ids existing in no vocabulary); scientific_preflight.cpp is
  owned by open PR #885 — entries recorded as OWNED-BY-#885.
- rs:sar_terrain_flatten accepts legacy `look_azimuth` not in schema — owned
  by #883 (file already modified there).
- test_capability_drift master-side failure (4 operators without knowledge
  entries) predates this branch.
