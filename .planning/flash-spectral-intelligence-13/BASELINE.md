# BASELINE — flash-spectral-intelligence-13 (Spectral Intelligence 13.0)

Track: GLM-5.3-Flash Track D — Hyperspectral Detection & Spectral-Spatial Intelligence 13.0
Branch: `agent/flash-spectral-intelligence-13`
Worktree: `/home/kevin/project/exp-rs-worktrees/flash-spectral-intelligence-13`

## Live master (read at Phase 0, 2026-09-21 ~03:00 CST)

- `origin/master` = `79adfe78a16b9419eef180cf9e6e5739658621a2` (2026-09-20 21:39 +0800,
  "Merge pull request #1134 from WindWang2/agent/flash-plugin-sdk-12").
- Local main checkout was behind (18e45a02); worktree created fresh from the fetched
  `origin/master` via `scripts/dev/new_worktree.py` (`git merge-base HEAD origin/master`
  == HEAD == 79adfe78, verified).
- Recent master commits (top): #1134 plugin-sdk-12, #1132 workflow-engine-12,
  #1131 sar-polsar-12, #1130 taskcenter-runtime-12, #1128 r2-verification-fixes.

## Open PRs (3) — full dedup in DEDUP.md

| PR | Branch | Files | Spectral-source overlap |
|---|---|---|---|
| #1135 temporal phenology 12.0 | agent/flash-temporal-phenology-12 | 77 | NO source overlap; YES generated-metadata overlap (census snap, contract_graph, capability sidecars, scientific_contract.cpp, meta-drift pin, pi/knowledge page) |
| #1136 classroom-safety 13.0 | agent/ds41-offline-labs-13 | 21 | none (claims ADR 0166; edits CLAUDE.md/repo-layout.md ADR count) |
| #1137 geospatial maintenance 13.0 | agent/ds41-geospatial-maintenance-13 | 13 | none (ledger tail + .gitignore tail only) |

Open issues: **0**.

## Direct predecessor

PR **#1119** "Spectral Intelligence 12.0 — CEM detection, spectral-spatial fusion,
library scale, coverage QA" (merged 2026-09-19, ADR 0165). Its stated non-goals are
exactly this track's work packages:
1. TCIMF/OSP detectors — deferred ("documented future extension on the same skeleton",
   ADR 0165:99-101);
2. Independent background raster — deferred (operator metadata still says
   "a separate background raster is a future extension",
   `src/operators/rs/rs_spectral_detection_operators.cpp:317,387-388`);
3. Edge-preserving/superpixel fusion — rejected for 12.0 ("contested structure-element
   semantics", ADR 0165:105-107);
4. Two spectral library implementations merged — deferred (census confirms
   `exp_spectral::SpectralLibrary` (src/core) vs `SpectralLibrary::Library`
   (src/processing/algorithms) are parallel implementations with different schemas).

## Remote branch residue

All other `agent/*` / `track/*` / `fix/*` remote branches are merged-equivalent or
superseded (classified via `git for-each-ref` ahead/behind against origin/master;
`scripts/dev/stale_branches.py` agrees). No in-flight branch claims TCIMF/OSP,
background-raster detection, edge-preserving fusion or library consolidation.

## Toolchain notes (this host)

- cmake: `/tmp/cmake-3.30.5-linux-x86_64/bin/cmake` (not on default PATH).
- GDAL + GSL resolve through `/home/kevin/pwb-sdks/root/usr` (same as sibling
  worktrees' CMakeCache): `-DGDAL_DIR=.../lib/cmake/gdal -DGSL_INCLUDE_DIR=... -DGSL_LIBRARY=...`.
- `ninja` is aliased to `ninja -j40` in interactive shells — never invoke bare
  `ninja`; always `cmake --build build-dev -j1/-j2`.
- Parallel epics on this host: other worktrees build concurrently; this track uses
  `-j1` (load/RSS monitored) per the global execution protocol.
