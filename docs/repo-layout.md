# Repository layout

Canonical map after the 2026-07-19 reorganization (Approach A).  
Design note: [superpowers/specs/2026-07-19-repo-layout-reorg-design.md](superpowers/specs/2026-07-19-repo-layout-reorg-design.md).

## Root (keep lean)

| Path | Role |
|------|------|
| `README.md`, `CLAUDE.md`, `CONTEXT.md` | Project entry docs (context = domain glossary + ADR index) |
| `CMakeLists.txt`, `cmake/`, `cmake_templates/` | Build system |
| `src/` | Application + libraries (all C++) |
| `tests/` | Catch2 unit / integration tests |
| `pi/` | Pi agent-runtime adapter (ADR 0122): `exp-rs-spatial.ts` MCP bridge + `knowledge/` |
| `models/` | Model runtime catalog manifests (`models/*/model.json`; weights never committed) |
| `external/` | Small vendored third-party sources |
| `resources/`, `images/` | App + QGIS icon/resource packs |
| `data/` | Config manifests, lab samples, local large rasters |
| `.planning/` | Per-track planning records (`<track>/{GOAL,PLAN,FINAL_REPORT,…}.md`); tracked per-track via `.gitignore` allow-list |
| `docs/` | All documentation |
| `scripts/`, `tools/`, `packaging/` | Build helpers, sample generators, AppImage |
| `itk_ref/`, `otb_ref/` | ITK / OTB source (CMake `add_subdirectory`; stay at root) |
| `refs/` | Optional local reference trees (gitignored) |
| `vendor/` | Optional GDAL/PROJ/GEOS/Boost header trees (source only) |
| `build*`, `cmake-build/` | Out-of-source builds (**never commit**) |

## `docs/`

| Path | Role |
|------|------|
| `docs/adr/` | ADR ledger `0001`–`0145` (one file per decision) |
| `docs/design/` | Product design (`DESIGN.md`), UI mockups (`ui/`) |
| `docs/architecture/` | QGIS/OTB implementation notes, phase reports |
| `docs/labs/` | Course / tutorial lab writeups |
| `docs/agent/` | Agent working memory (`task_plan`, `findings`, `progress`) |
| `docs/superpowers/` | Specs and implementation plans |
| `docs/*.md` | Shared utility docs (math-utils, gdal-utils, …) |

## `data/`

| Path | Tracked? | Role |
|------|----------|------|
| `data/processing/` | Yes | Toolbox coverage manifest + `algorithm_meta/` capability sidecars (ADR 0122) |
| `data/tools/custom/` | Yes | Generic CLI tool descriptors |
| `data/schemas/`, `data/pipelines/` | Prefer yes | Schemas / pipeline defs |
| `data/samples/` | Yes (small) | Lab rasters/vectors (was `samples_data/`) |
| Large ENVI/GF rasters under `data/` | No | Local-only; gitignored |

## `refs/` (local, gitignored)

| Path | Former name | Role |
|------|-------------|------|
| `refs/qgis/` | `qgis_ref/` | Full QGIS tree for reference / symbology XML |
| `refs/boost/` | `boost_ref/` | Optional Boost headers for OTB builds |

Runtime resolvers try `refs/qgis` first, then legacy `qgis_ref` and install `share/.../qgis_ref`.

## `src/` — data/display separation (Phase 1, ADR-0009)

| Path | Role |
|------|------|
| `src/data/` | `sicnu_data` — the project Data Manager: Data Asset identity, revision, leases, relocation, and GDAL/OGR source providers. Links `Qt6::Core` + `GDAL::GDAL` only; **no Qt Widgets / `qgis_gui`** (enforced by a CMake assertion). See `docs/superpowers/specs/2026-07-24-data-manager-architecture-spec.md`. |
| `src/app/display/` | `QgisDisplayManager` — owns Display Views and independent `QgsMapLayer`-backed Display Layers, one per presentation, each holding an Asset view lease. Built into `sicnu_qgis_display`. |
| `src/app/project_context.*` | `ProjectContext` — the per-project composition root owning one Data Manager + one Display Manager + the adoption safety net for legacy QGIS layers. |
| `src/app/data_project_serializer.*` | QGIS project (`.qgs/.qgz`) round trip: SICNU extension XML + standard-layer adoption. |
| `src/app/panels/data_manager_panel.*` | Data Manager asset-catalog dock, a read-only projection of asset snapshots, separate from the layer tree. |
| `src/app/active_view_host.*` | Active Display View host: open path / display asset on the active view (ex-LayerManager). |
| `src/agent/spatial_tools/` | Spatial Tool framework (ADR 0122): `SpatialTool` contract + registry + `spatial:` inspection/catalog tools; bridged into the Agent Tool Catalog by `SpatialToolProvider`. |
| `src/agent/mapspec/`, `src/agent/cartography/`, `data/cartography/` | MapSpec declarative cartography (ADR 0127/0130/0131): MapSpec 2.0 document model + compiler, design tokens, component/template registries, composition solver, preflight/repair quality gates; descriptors in `data/cartography/{tokens,components,templates}` (machine index `index.json`), docs in `docs/cartography/`. |
| `src/operators/framework/model_catalog.*` | Model runtime catalog (`ModelCatalog`) scanning `models/*/model.json`; `rs:infer` resolves catalog names. |
| `src/processing/framework/algorithm_meta_store.*` | Algorithm capability sidecar store (`AlgorithmMetaStore`) over `data/processing/algorithm_meta/*.json`. |

## `src/` — foundation & platform modules (8.x/9.0 tracks)

| Path | Role |
|------|------|
| `src/geospatial/` | `sicnu_geospatial` — Qt-free geospatial I/O foundation (ADR 0130/0134): canonical metadata, CRS policy, reader/writer contracts, certified format registry (`formats/`), COG validation (`cog/`), product adapters (`products/`), data doctor (`doctor/`), multidim semantics. |
| `src/geospatial/stac/` | Qt-free STAC client + item mapper (ADR 0130, superseding ADR 0050's `src/app/` placement; the Qt browser-dialog client remains `src/app/stac_client.*`). |
| `src/geospatial/remote/` | Remote I/O (ADR 0139): HTTP fetch, range cache (memory/disk), identity resolution + tokens, source validation. |
| `src/geospatial/util/` | Qt-free helpers: `sha256`, URI identity (`resource_uri`), time normalization, atomic FS writes. |
| `src/operators/` | RSOperator framework: `framework/` (operator base, JSON params, operation logger, model catalog) + `rs:` / `gdal:` / `otb:` / `opencv:` / `io:` operator families, `python/` (pybind bindings), `runtime/` (model inference: ONNX Runtime / OpenCV DNN / HTTP providers, tile inference). |
| `src/plugins/` | Plugin platform (ADR 0130 plugin lifecycle): lifecycle/adapter `framework/`, out-of-process `host/`, bundled `layer_tree/` + `processing/` plugins. |
| `src/runtime/` | Execution runtime: chunked tile pipeline (`chunk/`), GPU plane (`gpu/`), telemetry + fault registry (`observability/`), worker protocol (`worker/`). |
| `src/dataset/` | Dataset foundation (ADR 0134/0136): SQLite store for datasets/versions/samples/splits, label schemas, fingerprints, leakage/fold audits. |
| `src/experiment/` | Experiment foundation (ADR 0137/0138/0143): run identity/recording, metrics + evaluation, comparison, lineage + reproduction bundles, promotion. |
| `src/sdk/` | Headless/plugin SDK (`exprs/`): plugin discovery/loader/registry with manifests/permissions/quotas, safe external-process spawn, IPC framing, CLI exit-code contract, workflow schema/builder. |

## Icons symlink

`resources/icons` → `docs/design/ui/svg-icons/icons` (used by `resources/icons.qrc`).

## What not to commit

- Any `build/`, `build-*`, `cmake-build/` tree
- `vendor/*-prefix/`, `vendor/src/`, `vendor/CMakeCache.txt`, linked binaries under `vendor/`
- Multi-GB `refs/`, root screenshots, `*.log`, `*.so`, `symbology-style.db`
