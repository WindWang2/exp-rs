# PLAN — temporal-eo-phenology-change-10

Budget: 300M tokens, phase allocation per goal-template (0: 6%, WP 16/18/15/13,
tests+docs 11%, cross-review 8%, fixes 7%, PR 6%).

## Phase 0 — Baseline / archaeology / dedupe (done at commit-1)

- Baseline SHA + capability matrix + dedupe exclusions: `BASELINE.md`.
- Verified T-1/T-2/T-3/C-2 still open (file:line evidence).
- `.gitignore` whitelist + planning files committed first.

## Phase 1 — Contracts (WP-A start)

- `temporal_calendar.h/.cpp` kernel + pure known-answer tests
  (`test_temporal_calendar.cpp`).
- `temporal_change.h/.cpp` kernel (greedy harmonic+trend segmentation,
  magnitude/recovery) + `test_temporal_change.cpp`.
- `temporal_region_table.h/.cpp` kernel (streaming aggregation) +
  `test_temporal_regions.cpp` (pure parts).
- Extend `temporal_fit.h/.cpp`: `whittakerSmoothRobust`,
  `phenologyCycles` (multi-cycle).
- DECISIONS/ARCHITECTURE updated as contracts harden.

## Phase 2 — Operators (WP-A/B/C)

- `rs:temporal_regularize` (T-2) — operator + E2E tests.
- `rs:temporal_harmonic_breaks` (T-3) — operator + E2E tests.
- `rs:temporal_monitor` T-1 schema fix + test.
- `rs:temporal_smooth` whittaker_robust + `rs:temporal_phenology`
  cycles_per_year + tests.

## Phase 3 — Multi-ROI + features (WP-C)

- `rs:temporal_extract_regions` operator (regions JSON/regions_file,
  CSV+JSON outputs, cancellation, max_regions guard) + E2E tests.
- `rs:temporal_region_features` operator (feature table artifact) + E2E.

## Phase 4 — Integration

- Registry lines (integration commit), CMake source lists (integration
  commit), capability sidecars, knowledge appends,
  `docs/processing/temporal.md` contract rows, `docs/temporal/ARCHITECTURE_V3.md`,
  ADR file, CHANGELOG entry.

## Phase 5 — Scale / performance

- `benchmark_temporal10` (synthetic 1000-scene sparse collection; 100k
  region table; complexity + cancellation checkpoint evidence).
- Edge matrix: empty/single/all-NoData, NaN/Inf, corrupt metadata, duplicate
  instants, max-gap refusal, cancellation mid-stream (already kernel-level;
  operator-level coverage for new operators).

## Phase 6 — Cross review (≤2 read-only subagents)

- Subagent A: architecture + scientific correctness of full diff.
- Subagent B: adversarial test credibility + performance/lifecycle.

## Phase 7 — Fix findings

- P0/P1 must-fix; high-value P2 fixed; accepted debt recorded with reasons
  in `REVIEW_LOG.md`.

## Phase 8 — Final verification on final HEAD

- Rebuild targeted, rerun all temporal tests, `git diff --check`,
  conflict-marker scan, existence assertions, evidence re-pin.

## Phase 9 — PR

- Rebase on origin/master (safe window), resolve, re-verify, push,
  `gh pr create --base master`, never merge.
