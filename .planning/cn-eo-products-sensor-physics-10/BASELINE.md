# BASELINE — cn-eo-products-sensor-physics-10

- **Baseline SHA**: `7d78059d1a6d316d606656759a506d17bc5e3b55` (origin/master, 2026-09-13)
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-cn-eo-products-sensor-physics-10`
- **Branch**: `zcode/cn-eo-products-sensor-physics-10`
- **Verification**: `git rev-parse origin/master` → `7d78059d1a…` (executed 2026-09-13, exit 0)

## Most relevant merged PRs

| PR | Title | Relevance |
| --- | --- | --- |
| #956 | feat(products): GF / ZY-3 / HJ-1 product adapters and import operators | Direct predecessor; must not redo |
| #955 | feat(spectral): built-in redistributable spectral library and material prior API | Owns `data/spectral/sensors.json` + `SensorProfile` resampling API |
| #950 | feat(agent): capability knowledge layer for all 111 rs operators | Owns `data/processing/algorithm_meta/capability/` schema |
| #957 | review: whole-repository line-by-line audit dossier | Findings baseline (no open P0/P1 in products area) |
| #958 | docs(agents): /goal and /loop command system review | Process conventions |

## Dedupe exclusions (do not re-implement)

- GF-1/2/6 PMS/WFV + ZY-3 TLC/NAD/FWD/BWD + HJ-1A/1B CCD identity/parsing/imports (#956).
- CRESDA sidecar generation handling (legacy `<MetaInfo>` + current `<ProductMetaData>`
  tag whitelists) — extend, do not fork.
- `band_roles/*.json` fail-closed loader + `unknown` role-reason contract.
- `SatelliteProducts::stackToGeoTiff` stacking contract (windowed, fail-closed).
- Sentinel/Landsat/MODIS adapters (`product_adapters.cpp`) — other families' authority.
- Spectral library `SensorProfile::resampleTo` Gaussian SRF (ADR 0079) — consume.

## Concurrent tracks

- `zcode/scientific-contract-verification-10` worktree exists (clean at baseline).
  Shared-file conflict risk assessed as low (that track owns contracts/verification docs,
  not `src/geospatial/products/`). Monitor at rebase time.
- 9 other 10.0 tracks implied by the brief; append-only discipline on shared files
  (`data/processing/algorithm_meta/`, `docs/adr/`) applies.

## Open review findings affecting this track

- `review/findings/F-OPS-1..5`, `F-PI-1/2` — none touch `src/geospatial/products/`
  (verified by grep 2026-09-13). No inherited defects.

## Local git note

`~/.git/info/exclude` contains `.planning/` (machine-local, predates this track; same
situation recorded in master commit `4c9a22ab0c`). Planning files are force-added
(`git add -f`); the tracked `.gitignore` whitelist for this track is in place and is
what other clones see.
