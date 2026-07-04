# Processing Toolbox Phase 1: Algorithm Coverage

**Date:** 2026-07-03  
**Status:** Approved  
**Scope:** Phase 1 of multi-phase Toolbox optimization (user selected F → Phase 1 = A)

---

## Problem Statement

The Processing Toolbox has a working UI (`QgsProcessingToolboxTreeView` in the right dock), four providers (`qgis_algorithms`, `gdal_tools`, `otb_tools`, `generic_cli`), and a solid execution path (`SicnuAlgorithmDialog`). However, algorithm coverage across GDAL raster/vector and OTB remote-sensing categories remains incomplete relative to the inventory in `docs/superpowers/plans/2026-05-31-processing-toolbox-enhancements.md`.

Remote-sensing workflows also suffer from **dual entry points**: many Raster menu items open bespoke dialogs while similar capabilities exist (or should exist) in the Toolbox. Phase 1 does **not** unify those paths; it ensures the Toolbox catalog is complete and trustworthy first.

---

## Goals (Phase 1)

| Goal | Metric |
|------|--------|
| Category coverage | Categories 1–5 each ≥ **80%** of manifest `required` algorithm IDs registered |
| Hand-crafted quality | **18** high-frequency RS tools meet full quality bar (see below) |
| Long-tail coverage | Remaining manifest gaps filled via **Generic CLI JSON** in `tools/custom/` |
| Regression safety | CI test `test_toolbox_coverage` fails on coverage drop |

### Out of Scope (Phase 2+)

- Toolbox UI reorganization, RS-specific grouping, favorites UX (Phase 2 / item B)
- Execution polish: auto-add result layers, unified progress (Phase 3 / item C)
- Raster menu ↔ Toolbox deduplication (Phase 4 / item D)
- Guided Workflows + MCP deep integration (Phase 5 / item E)

---

## Approach: Manifest-Driven Hybrid (Recommended)

Three strategies were considered:

1. **Manifest-driven hybrid (chosen)** — single `toolbox_manifest.json` truth source; hand wrappers for top tools; Generic CLI for long tail; CI gate.
2. **Parallel category sprints** — fast but inconsistent quality and testing.
3. **Runtime-only registration** — hide missing CLIs; poor for fixed 80% target in teaching labs.

Optional Phase 1.5 enhancement: surface GDAL/OTB path status in Preferences using `ToolPathManager`.

---

## Category Definitions

Aligned with user-selected priorities 1–5:

| ID | Category | Examples |
|----|----------|----------|
| `gdal_raster_basic` | Raster basics | translate, warp, info, merge, buildvrt, retile, addo |
| `gdal_raster_analysis` | Raster analysis | calc, dem, contour, proximity, sieve, fillnodata, grid, rasterize |
| `gdal_vector` | Vector basics | ogr2ogr, ogrinfo, polygonize, tindex |
| `otb_rs` | OTB remote sensing | band math, indices, segmentation, classification, feature extraction, texture |
| `otb_preprocess` | OTB preprocessing | ortho, pansharpen, superimpose, smoothing, statistics |

---

## Current Baseline (2026-07-03)

Approximate registered counts:

| Provider | Registered | Notes |
|----------|-------------|-------|
| `qgis_algorithms` | ~40 | Includes RS natives (band_math, spectral_index, atmospheric_correction) |
| `gdal_tools` | ~18 | Core raster + vector CLIs wrapped |
| `otb_tools` | ~22 | Strong OTB coverage via `OtbToolWrapper` |
| `generic_cli` | Variable | JSON-defined tools from `~/.sicnu_geo_rs/tools/` and `tools/custom/` |

Toolbox UI (`src/app/main_window_docks.cpp`): search filter, double-click → `SicnuAlgorithmDialog`, favorites context menu. No UI changes in Phase 1.

---

## Hand-Crafted Wrapper Tier (18 tools)

Full quality bar for each:

- Complete `initAlgorithm()` parameters with descriptions
- `shortHelpString()`, semantic `tags()`, Agent metadata where applicable
- Unit test: metadata + `buildArgs` (CLI wrappers) or `processAlgorithm` smoke (native)
- `toJsonSchema()` export succeeds
- Clear error when `ToolPathManager` cannot resolve binary

### GDAL (6 new or upgraded)

| Tool | Action |
|------|--------|
| `gdaladdo` | New hand wrapper |
| `gdaltransform` | New hand wrapper |
| `gdal_edit` | New hand wrapper |
| `pct2rgb` | New hand wrapper |
| `rgb2pct` | New hand wrapper |
| `gdal2xyz` | New hand wrapper |

### OTB (8 new or upgraded)

| Tool | Action |
|------|--------|
| `ComputeImagesStatistics` | Upgrade params + tests (exists) |
| `ReadImageInfo` | Upgrade structured output + tests (exists) |
| `PixelInfo` | Upgrade + tests (exists) |
| `GrayLevelCooccurrenceMatrix` | New hand wrapper |
| `LocalStatisticExtraction` | New hand wrapper |
| `SVMClassification` or `TrainImagesClassifier` | New hand wrapper (whichever OTB CLI available) |
| `MultivariateAlterationDetector` | New hand wrapper |
| `StereoRectification` | New hand wrapper if OTB ships binary; else Generic CLI fallback |

### QGIS RS Native (4 quality upgrades)

Existing algorithms — align parameters with menu dialogs, add/extend tests:

| Algorithm | ID prefix |
|-----------|-----------|
| Band Math | `qgis_algorithms:band_math` |
| Spectral Index | `qgis_algorithms:spectral_index` |
| Atmospheric Correction | `qgis_algorithms:atmospheric_correction` |
| Raster Statistics | `qgis_algorithms:raster_statistics` |

---

## Generic CLI Tier

JSON definitions under `tools/custom/` (and optionally shipped samples under `data/tools/custom/`).

Phase 1 minimum for each Generic CLI tool:

- Valid `id`, `displayName`, `group`, `command`, `args` template
- Visible and searchable in Toolbox
- INPUT/OUTPUT parameters functional
- Tagged as generic (no full parameter UI required)

Candidate long-tail tools (manifest-driven, ~15–25):

- `gdal2tiles`
- `otb_bilateral_filter`
- `otb_median_filter`
- `otb_block_matching`
- `otb_disparity_map_to_elevation`
- Additional entries from gap analysis against `2026-05-31-processing-toolbox-enhancements.md`

---

## Coverage Manifest & CI Gate

**New file:** `data/processing/toolbox_manifest.json`

Structure:

```json
{
  "version": 1,
  "categories": {
    "gdal_raster_basic": {
      "target_pct": 80,
      "required": ["gdal:gdal_translate", "gdal:gdalwarp", "..."]
    }
  },
  "handcrafted_required": [
    "gdal:gdaladdo",
    "otb:otb_compute_images_statistics",
    "..."
  ]
}
```

**New test:** `tests/test_toolbox_coverage.cpp`

- Loads manifest
- For each category: `registered / required >= target_pct`
- For `handcrafted_required`: every ID resolves via `processingRegistry()->algorithmById()`
- On failure: print missing IDs (developer-actionable)

Existing tests retained: `test_algorithm_organization`, `test_algorithm_schema`, provider smoke tests.

---

## Architecture

No structural changes to providers:

```
QgsProcessingRegistry
├── qgis_algorithms/   (4 RS upgrades)
├── gdal_tools/        (+6 hand, manifest gaps via generic)
├── otb_tools/         (+6 hand, manifest gaps via generic)
└── generic_cli/       (JSON bulk registration)
```

Execution path unchanged:

```
Toolbox double-click → openProcessingAlgorithm(id)
                    → SicnuAlgorithmDialog
                    → QgsProcessingRegistry::execute
```

`ToolPathManager` remains the single CLI resolution layer (`SICNU_GDAL_PATH`, `SICNU_OTB_PATH`, bundled `tools/gdal`, `tools/otb`).

---

## Error Handling

| Condition | Behavior |
|-----------|----------|
| CLI binary missing | Algorithm dialog shows error via `OtbToolWrapper` / `GdalToolWrapper` feedback; log `SicnuLogTags::OTB` / `GDAL` |
| Generic CLI JSON invalid | Skip file at load; `QMessageBox` warning (existing `GenericCliProvider` behavior) |
| Manifest ID typo | CI fails with explicit missing ID list |

Phase 1.5 (optional): `canExecute()` or disabled state in Toolbox when binary probe fails — not required for Phase 1 completion.

---

## Multi-Phase Roadmap (Context)

| Phase | Theme | Depends on |
|-------|-------|------------|
| **1** | Algorithm coverage (this spec) | — |
| **2** | Discovery & organization (RS groups, tags) | manifest |
| **3** | Execution experience | — |
| **4** | Menu ↔ Toolbox deduplication | Phase 1 RS stability |
| **5** | Education + MCP integration | Phase 2 |

---

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| OTB not installed in lab | Tools fail at run time | Clear errors; Preferences path hint; stereo → Generic CLI fallback |
| 80% met only by low-quality Generic CLI | Weak teaching value | Separate `handcrafted_required` CI list (18 must pass) |
| Manifest drift | False green CI | Manifest is sole coverage truth; updated in same PR as new algorithms |
| Duplicate algorithm IDs | Registry conflicts | Existing `test_algorithm_organization` unique-ID check |

---

## Done When

- [ ] `data/processing/toolbox_manifest.json` committed with categories 1–5
- [ ] Each category ≥ 80% manifest coverage (CI enforced)
- [ ] All 18 hand-crafted / upgraded tools pass dedicated tests
- [ ] Generic CLI pack registers remaining manifest gaps
- [ ] Full ctest green (no regression)
- [ ] README or CLAUDE.md notes manifest + coverage test command

---

## References

- `docs/superpowers/plans/2026-05-31-processing-toolbox-enhancements.md` — inventory checklist
- `docs/superpowers/specs/2026-05-29-processing-toolbox-design.md` — original multi-provider architecture
- `src/app/main_window_docks.cpp` — Toolbox dock wiring
- `src/app/dialogs/sicnu_algorithm_dialog.{h,cpp}` — execution dialog
- `src/processing/providers/{gdal_tools,otb_tools,generic_cli}/` — provider implementations