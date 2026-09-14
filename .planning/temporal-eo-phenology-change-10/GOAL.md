# GOAL — Temporal EO / Phenology / Change Intelligence Platform 10.0

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> Track ID: `temporal-eo-phenology-change-10`
> **Track branch:** `zcode/temporal-eo-phenology-change-10` (worktree `../exp-rs-temporal-eo-phenology-change-10`, off `origin/master` @ `7d78059d1a6d316d606656759a506d17bc5e3b55`)
> **Mode:** unattended long-running epic. Local build/test evidence only — never block on, trigger, or cite online CI.
> **Write scope:** `src/processing/algorithms/temporal/`, `src/operators/rs/rs_temporal_*`, `src/operators/rs/rs_operators_init.cpp` (append-only registration), `tests/` (new/extended temporal tests), `tests/CMakeLists.txt` (append-only), `docs/processing/temporal.md`, `docs/temporal/`, `docs/adr/`, `CHANGELOG.md`, `.gitignore` (whitelist line), `data/agent/knowledge/` + `data/processing/algorithm_meta/` (temporal entries only), `benchmarks/` via new files registered in `tests/CMakeLists.txt`.
> **Read-only:** `src/core/**`, `src/gui/**`, `src/app/**` (workbench-9 ownership), `src/processing/algorithms/sar/**` (advanced-sar-polsar-insar-10 ownership), `src/operators/rs/rs_sar_*` (same), `src/geospatial/**` + `src/io/**` (cloud-data-fabric-datacube-10 ownership), `src/dataset/**`, `src/experiment/**` (dataset-experiment-mlops ownership), `src/operators/runtime/**` (model runtime ownership), `master` branch.

## Mission

The repo already ships a streaming multi-temporal operator family (Temporal 1.0–3.0, PRs #712/#732/#733/#738/#742): `rs:temporal_{summary,composite,index_series,trend,smooth,gap_fill,harmonic_fit,phenology,breakpoints,sen_trend,decompose,anomaly,extract_series,monitor}` over the `TemporalCollection` contract with ADR 0125 workspace records and STAC ingestion. Verified gaps at baseline (`7d78059d1a`): `rs:temporal_monitor` refuses `scenes` arrays (ISSUES T-1, `rs_temporal_monitor_operator.cpp:77`); gap fill cannot emit a regular calendar (T-2, output bands = input dates); there is no joint seasonal+trend change model (T-3 — harmonic fit is global, breakpoints are piecewise-linear only); extraction handles exactly one point or one polygon (C-2, `rs_temporal_extract_series_operator.cpp:118`); phenology covers a single season window (`temporal_fit.h:60`); no ML-consumable temporal feature artifact exists.

This track closes those gaps as one platform increment: a regular-calendar time-normalization layer, an honest joint harmonic+trend break model with disturbance semantics, first-class multi-region temporal extraction, multi-cycle phenology, and machine-readable region feature tables. After this track, an agent or pipeline can move from "irregular scene collection" to "regular calendar → gap-filled → phenology/trend/change features per region → ML table" without leaving the temporal operator family.

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended. No clarifying questions. Every taken decision is recorded in `DECISIONS.md`.
- **Subagents: at most 2**, both read-only (Subagent A: architecture + scientific-correctness review of the track diff; Subagent B: adversarial review of tests/performance/lifecycle). Main agent owns 100% of implementation and judgment. No nested subagents.
- **No CI**: never wait on, trigger, or cite GitHub Actions. Local evidence only → `EVIDENCE.md`.
- **Build resources (hard)**: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, `-j2` (drop to `-j1` under RSS/load pressure), `-j$(nproc)` forbidden, `QT_QPA_PLATFORM=offscreen`, targeted `ctest -R <family> -j1` first.
- **Master is read-only.** All work in the track worktree; phase commits; final PR not merged.

## Autonomy defaults

1. **格式/来源**: new kernels follow `temporal_fit.h` contracts (NaN = missing, real day offsets, deterministic); new operators follow `rs_schema.h` builder + `REGISTER_RS_OPERATOR` precedent; JSON via nlohmann `Json::Value`.
2. **失败项处置**: a kernel whose scientific preconditions fail returns NaN/refuses per contract; a test that cannot run on this host is marked `not-executed` with its reason — never claimed.
3. **命名/编号**: new operators use `rs:temporal_<verb>` ids consistent with the family; honest method names (no "CCDC"/"BFAST" claims — "harmonic+trend greedy segmentation" wording in descriptions).
4. **资源与超时**: single command timeout 600 s for tests, background for long builds; disk guard: abort new build artifacts if `/` free < 8 GB.
5. **对外动作**: `git fetch` allowed (read-only); final `git push` + `gh pr create` per runbook.
6. **范围外发现**: recorded in `EVIDENCE.md` `OUT_OF_SCOPE`; P0-level items also flagged at the top of `PR_BODY.md`.
7. **依赖新增**: none. Reuse GDAL/jsoncpp/Catch2 only.

## Work packages

| ID | Package | Key deliverables |
|---|---|---|
| A | Time normalization (T-1, T-2) | `temporal_calendar` kernel; `rs:temporal_regularize` operator; monitor accepts scenes |
| B | Seasonal-trend change + phenology (T-3) | harmonic+trend segmentation kernel + `rs:temporal_harmonic_breaks` operator with disturbance metrics; multi-cycle phenology; robust Whittaker in `rs:temporal_smooth` |
| C | Multi-ROI + features (C-2, G) | `rs:temporal_extract_regions` streaming zonal operator; `rs:temporal_region_features` ML table operator |
| D | Integration + scale + docs | registry/help/knowledge sidecars, `docs/processing/temporal.md` contracts, ARCHITECTURE_V3, benchmark, CHANGELOG |

## Completion gate

1. `rs:temporal_monitor` schema accepts `scenes` (schema diff + test) — verify `ctest -R test_temporal_algorithms`.
2. Regular calendar + gap fill executable end-to-end on synthetic irregular collection — new kernel tests + operator E2E.
3. At least one honest joint trend+seasonal change model executable (`rs:temporal_harmonic_breaks`) with known-answer tests.
4. Multi-ROI temporal extraction is first-class (`rs:temporal_extract_regions`) with region ids, bounded memory, cancellation tests.
5. Temporal feature table artifact consumable downstream (CSV/JSON with stable schema + documented consumer path).
6. All P0/P1 review findings closed with evidence in `REVIEW_LOG.md`.
7. Final local verification re-run on PR-final HEAD; PR created, not merged.

## PR runbook

Per `docs/agents/goal-template.md` runbook 1–10: worktree first commit includes `.gitignore` whitelist + planning docs; phase commits; evidence pinned per phase; existence assertions before final commit; `git push -u origin zcode/temporal-eo-phenology-change-10`; `gh pr create --base master`; never merge; reviewer requests continue in-track.
