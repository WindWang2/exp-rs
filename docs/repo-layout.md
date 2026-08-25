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
| `docs/` | All documentation |
| `scripts/`, `tools/`, `packaging/` | Build helpers, sample generators, AppImage |
| `itk_ref/`, `otb_ref/` | ITK / OTB source (CMake `add_subdirectory`; stay at root) |
| `refs/` | Optional local reference trees (gitignored) |
| `vendor/` | Optional GDAL/PROJ/GEOS/Boost header trees (source only) |
| `build*`, `cmake-build/` | Out-of-source builds (**never commit**) |

## `docs/`

| Path | Role |
|------|------|
| `docs/adr/` | ADR ledger `0001`–`0122` (one file per decision) |
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
| `src/operators/framework/model_catalog.*` | Model runtime catalog (`ModelCatalog`) scanning `models/*/model.json`; `rs:infer` resolves catalog names. |
| `src/processing/framework/algorithm_meta_store.*` | Algorithm capability sidecar store (`AlgorithmMetaStore`) over `data/processing/algorithm_meta/*.json`. |

## Icons symlink

`resources/icons` → `docs/design/ui/svg-icons/icons` (used by `resources/icons.qrc`).

## What not to commit

- Any `build/`, `build-*`, `cmake-build/` tree
- `vendor/*-prefix/`, `vendor/src/`, `vendor/CMakeCache.txt`, linked binaries under `vendor/`
- Multi-GB `refs/`, root screenshots, `*.log`, `*.so`, `symbology-style.db`
