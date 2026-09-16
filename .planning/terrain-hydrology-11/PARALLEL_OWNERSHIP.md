# PARALLEL_OWNERSHIP — terrain-hydrology-11 (2026-09-15)

## Open PRs / remote branches at start

| Ref | State | Changed files (relevant) | Overlap with this track | Policy |
|---|---|---|---|---|
| PR #1008 `zcode/radiometric-spectral-workbench` | open, CONFLICTING vs master | `src/agent/spatial_tools/spatial_tool.cpp`, `src/agent/spatial_tools/spectral_spatial_tools.*`, `src/agent/CMakeLists.txt`, `src/analysis/*`, `src/core/radiometric_state.*`, `src/core/spectral_library.*`, `src/app/widgets/spectral_*`, `src/app/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/processing/algorithms/radiometric_calibration.*`, `spectral_indices.*`, `spectral_unmixing.*`, `tests/CMakeLists.txt`, `.gitignore`, docs/adr/0158 | **No terrain business file overlap.** Shared integration files I may also touch minimally: `src/agent/spatial_tools/spatial_tool.cpp` (tool registration lines), `tests/CMakeLists.txt`, `src/app/CMakeLists.txt`, `src/agent/CMakeLists.txt`, `.gitignore` | Treat as read-only; my edits to shared files are append-only new registration/target lines. If rebase conflicts arise, re-derive from new master; never delete their lines. |
| `master` branch | read-only | — | baseline | rebase target after every phase commit |

## Remote branches (git branch -r, recent)

All recent `zcode/*` and `grok/*` branches correspond to merged (or open #1008) tracks;
none owns terrain files. `docs/` grep confirms no other track is authoring
`docs/processing/terrain*`.

## Issue dedupe

Open issues #1001–#1007: io/workflow/dataset/georef domains — no terrain overlap;
recorded as OUT_OF_SCOPE for this track (see EVIDENCE.md).

## This track's write scope (narrowed from prompt, per audit)

**Business (exclusive):**
- `src/processing/algorithms/terrain_flow.{h,cpp}` (extend)
- `src/processing/algorithms/terrain_analysis.{h,cpp}` (extend: landform products)
- `src/processing/algorithms/terrain_hydrology.{h,cpp}` (new: flat resolution, D∞,
  streams/Strahler, outlets)
- `src/processing/algorithms/terrain_viewshed.{h,cpp}` (new)
- `src/processing/algorithms/terrain_solar.{h,cpp}` (new)
- `src/processing/algorithms/terrain_landform.{h,cpp}` (new)
- `src/operators/rs/rs_terrain_flow_operator.*`, `rs_terrain_analysis_operator.*` (extend)
- `src/operators/rs/rs_terrain_viewshed_operator.*` (new)
- `src/operators/rs/rs_terrain_solar_operator.*` (new)
- `src/app/dialogs/terrain_dialog.*` (extend)
- `tests/test_terrain*.cpp`, `tests/synthetic_terrain_dem.h` (new/extend)
- `docs/processing/terrain-analytics.md` (new), `docs/processing/foundation-5.md`
  (debt-note update)
- `data/processing/algorithm_meta/capability/rs-terrain-*.json` (regen after product changes)

**Shared integration (append-only, minimal):**
- `src/processing/CMakeLists.txt` (source list)
- `src/operators/rs/CMakeLists.txt` (new operator sources)
- `src/app/CMakeLists.txt` (dialog already compiled — verify; add only if needed)
- `src/agent/spatial_tools/terrain_spatial_tools.{h,cpp}` (new) + 2 registration
  lines in `src/agent/spatial_tools/spatial_tool.cpp` + `src/agent/CMakeLists.txt`
- `tests/CMakeLists.txt` (new test targets)
- `CHANGELOG.md` (entry)
- `.gitignore` (already done: `.planning/terrain-hydrology-11/` whitelist)
- `README.md` (terrain line update — append/edit one line; low-conflict file)

**Read-only / avoid:** all `src/agent/harness/*` (D17/D18), `src/experiment/*`,
`src/dataset/*` (D19), all radiometric/spectral files (PR #1008), `src/workflow/*`.
