# Scope & Baseline — exp-rs Audit

## Repository
- URL: https://github.com/WindWang2/exp-rs
- Branch audited: origin/master
- BASE_SHA: `19843d1b6910c9207c7e5c97863a873db679368e`
- Date frozen: 2026-08-15

## Audit worktree
- Path: /home/kevin/projects/rs-studio/main/.scratch/audit-worktree
- Mode: detached HEAD at BASE_SHA
- master checkout: untouched (only pre-existing in-progress user modifications)

## Toolchain (host)
| Tool | Version | Notes |
|---|---|---|
| GCC | 16.1.1 | ZCode AppImage (16.1.1 20260728) |
| CMake | 4.4.2 | `cmake` works, no `CMAKE_ROOT` set |
| Qt | 5.15.19 (default `qmake`) + Qt6 available at /usr/lib64/qt6 | repo pins Qt6 ≥ 6.8 |
| GDAL | 3.13.2 "Iowa City" 2026/07/20 | gdalinfo found |
| PROJ | 9.8.1 | no direct `proj_*` calls in either module |
| OpenCV | not present in pkg-config | repo requires it (build will gate) |

## Repository shape
- Project: `sicnu_geo` v1.0 — heavily-forked QGIS 4.0.2 (vendored commit 0b7a0dbf654c4e47249d9469f20c4d8ecccfba62)
- `qgis_core` / `qgis_gui`: shared libs (vendored)
- `qgis_analysis`, `qgis_app_georef`, `qgis_app_classify`, `qgis_app_obia`, `sicnu_*`: static libs
- Executables: `sicnu_geo_rs` (GUI), `sicnu_geo_rs_cli` (headless)
- In-process Qt plugins: `layer_tree`, `processing` (via `SicnuPluginInterface`)
- Tests: ~219 Catch2 executables
- 30/30 recent issues open, all English, severity-tagged `[Pn][category]` (created 2026-08-14)

## Modules in scope
1. `src/analysis/classification/` — 51 files (.cpp+.h), ~5,539 LoC analysis layer
2. `src/app/classification/` — 47 files, ~8,257 LoC Qt UI
3. `src/analysis/georeferencing/` — 8 files, ~1,849 LoC (QGIS upstream port)
4. `src/app/georeferencer/` — relevant (full call graph mapped in callchains.md)
5. `src/ui/georeferencer/` — 5 `.ui` files; only `qgsmapcoordsdialogbase.ui` is live

## Out of scope (for this audit)
- `src/core/`, `src/gui/`, `src/external/` (vendored QGIS)
- `src/analysis/segmentation/` (OBIA, separate audit)
- `src/operators/rs/*` (algorithmic operators, partial coverage via classification seams)
- Plugin host / MCP server

## Permissions granted for this audit
- `gh` authenticated as `WindWang2` with full scopes (issues can be created)
- All Issue body text will be in English (matches recent issue language)
- Issue labels: reuse existing only — currently no severity/type/area labels exist
