# Repository layout

Canonical map after the 2026-07-19 reorganization (Approach A).  
Design note: [superpowers/specs/2026-07-19-repo-layout-reorg-design.md](superpowers/specs/2026-07-19-repo-layout-reorg-design.md).

## Root (keep lean)

| Path | Role |
|------|------|
| `README.md`, `CLAUDE.md` | Project entry docs |
| `CMakeLists.txt`, `cmake/`, `cmake_templates/` | Build system |
| `src/` | Application + libraries (all C++) |
| `tests/` | Catch2 unit / integration tests |
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
| `docs/design/` | Product design (`DESIGN.md`), UI mockups (`ui/`) |
| `docs/architecture/` | QGIS/OTB implementation notes, phase reports |
| `docs/labs/` | Course / tutorial lab writeups |
| `docs/agent/` | Agent working memory (`task_plan`, `findings`, `progress`) |
| `docs/superpowers/` | Specs and implementation plans |
| `docs/*.md` | Shared utility docs (math-utils, gdal-utils, …) |

## `data/`

| Path | Tracked? | Role |
|------|----------|------|
| `data/processing/` | Yes | Toolbox coverage manifest |
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

## Icons symlink

`resources/icons` → `docs/design/ui/svg-icons/icons` (used by `resources/icons.qrc`).

## What not to commit

- Any `build/`, `build-*`, `cmake-build/` tree
- `vendor/*-prefix/`, `vendor/src/`, `vendor/CMakeCache.txt`, linked binaries under `vendor/`
- Multi-GB `refs/`, root screenshots, `*.log`, `*.so`, `symbology-style.db`
