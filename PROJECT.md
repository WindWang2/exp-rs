# Project: SICNU GEO RS (exp-rs)

Pure C++20 remote-sensing analysis platform built on the QGIS engine. One
execution seam for every frontend: GUI, headless CLI, Workflow DAGs, MCP/Pi
agents, and third-party SDK plugins all reach algorithms through the
`RSOperator` registry and Task Center.

## Status (living view — details live in CONTEXT.md, docs/adr/, CHANGELOG.md)

- **Processing & algorithms**: `rs:` operator family (raster math, spectral
  indices/analysis, classification, OBIA, change detection, SAR, temporal,
  feature engineering, terrain, fusion, product import, model inference) over
  GUI-free kernels in `src/processing/algorithms/`. Shared scientific
  semantics (NoData, valid-observation statistics, grid preflight,
  radiometric scale/offset, histogram/threshold binning) are centralized
  primitives; per-operator determinism grades follow ADR 0124. Scientific
  policies: `docs/processing/`.
- **Temporal**: collection model, STAC ingestion, streaming operators
  (summary/composite/trend/OLS+Sen/harmonic/phenology/breakpoints/decompose/
  anomaly/gap-fill/smooth/extract) with real-acquisition-time semantics.
- **Agent surfaces**: MCP server, unified tool catalog, spatial inspection
  tools, MapSpec cartography, workflow preflight/repair — ADR 0120–0128.
- **Governance**: workspace identity, SQLite governance store, project format
  v3, atomic project save, lineage, smart collections — ADR 0129.
- **SDK/plugins**: C++/Python plugin hosts, headless automation, plugin SDK.

## Architecture

- Repository: `exp-rs` (C++20 / Qt 6.8+ / GDAL / PROJ / GEOS / OpenCV 5 /
  Catch2 v3.7.1); default branch `master`.
- Libraries: `src/core` (QGIS core), `src/gui` (QGIS GUI), `src/data`,
  `src/processing` (algorithms, GDAL wrappers, Tool Call Dispatcher, Task
  Center), `src/operators` (RSOperator framework + `rs:`/`gdal:`/`otb:`/
  `opencv:` families), `src/analysis` (classification, segmentation,
  georeferencing), `src/workflow`, `src/jobs`, `src/agent`.
- Applications: `src/app` (`sicnu_geo_rs` desktop), `src/cli`
  (`sicnu_geo_rs_cli` headless).
- Layout map: `docs/repo-layout.md`; domain vocabulary + ADR index:
  `CONTEXT.md`; ADR records: `docs/adr/`.

## Build & test contract

- Build: `cmake --build build` (Release + Ninja; presets in
  `CMakePresets.json`: `dev-default`, `ci-fast`, `ci-full`, `sanitizer-debug`,
  `release-package`).
- Test runner: `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest
  --test-dir build --output-on-failure` — CTestCustom.cmake pins
  `PYTHONHOME`/`PYTHONPATH` and `QT_IM_MODULE=compose`; see `TEST_INFRA.md`
  for the environment policy and the history of the stale `LD_PRELOAD`
  workaround. Treat "fully green" claims as valid only with a fresh ctest log.
- Scientific validation policy and tolerance grades for algorithm kernels:
  `docs/processing/validation-policy.md`.
