# Project: exp-rs — SICNU GEO RS Platform

C++20 remote-sensing analysis platform on the QGIS engine (Qt 6.8+, GDAL, PROJ,
GEOS, OpenCV 5, Catch2 v3). See `README.md` for the architecture map and
`CONTEXT.md` for domain vocabulary and the ADR ledger.

## Platform state (durable capabilities)

- **Execution**: Task Center owns algorithm-task lifecycle (queue/priority/
  pause/cancel, resource admission, RSS throttling) and delegates to JobEngine
  (in-process workers) or opt-in isolated workers (`sicnu_worker` binary via
  worker protocol v1; one-shot host or bounded warm `LocalWorkerPool`).
- **Workflow**: Workflow Engine 2.0 — per-transition atomic checkpoints,
  crash recovery + resumable interrupted runs, port-aware placeholder
  resolution, ArtifactGC, deterministic execution cache (contract v2) with an
  optional content-addressed persistent tier, and crash-resume completion
  identity (Reliability 4.0).
- **Data plane**: DataManager as the runtime asset authority (AssetId +
  revision identity, leases, derivations, temporal collections); external
  content changes advance revisions through a bounded watcher;
  ArtifactStore + ArtifactObjectPool provide content-addressed storage with
  reference-safe eviction (Reliability 4.0).
- **Governance**: project format v3 (`<workspace>` governed document as the
  authority; SQLite WAL store as a derived index), datasets/results/runs/
  experiments/smart collections/exports, lineage graph, snapshots
  (WAL-checkpointed), relink, validation, bounded import, repro bundles,
  truthful workflow-run states (ADR 0129/0130).
- **Reliability contracts**: `docs/architecture/FAULT_MATRIX_4.md` (fault
  rows → expected safe behavior → covering tests); scale contracts are pinned
  by `tests/test_workspace_stress` (100k routine, opt-in 1M stress) with the
  current baseline in `.planning/data-runtime-governance-4/SCALE_BASELINE.md`.

## Build & test policy

- Configure: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON` (ccache launchers
  recommended). Presets in `CMakePresets.json` (`dev-default`, `ci-fast`,
  `ci-full`, `sanitizer-debug`).
- Test runner: `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest
  --test-dir build --output-on-failure`; `CTestCustom.cmake` pins
  PYTHONHOME/PYTHONPATH/QT_IM_MODULE. Do not quote suite-wide pass counts
  without a fresh local `ctest` log.
- Large workspaces: keep heavy/stress suites sequential; never materialize
  the 1M stress tier in GUI/service layers.

## Documentation map

- ADRs: `docs/adr/` (0001–0130; 0129 governance platform, 0130 reliability).
- Domain terms: `CONTEXT.md` (single-context root).
- Developer map: `docs/repo-layout.md`, `CLAUDE.md`.
- Failure/scale behavior: `docs/architecture/FAULT_MATRIX_4.md`,
  `.planning/data-runtime-governance-4/` (goal dossier with dated evidence).
