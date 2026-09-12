# OVERLAP MAP — parallel-track conflict avoidance

This track runs alongside ~9 other 9.0 worktrees. Files are classified:

## Exclusively ours (safe to edit)

- `src/processing/algorithms/**` (kernels + headers)
- `src/operators/rs/**` (algorithm semantics of RS operators)
- `src/processing/contracts/**`
- `docs/processing/**`
- `.planning/scientific-algorithms-9/**`
- new test files `tests/test_*` created here

The uncommitted defect-sweep in the shared `main` checkout touches some of
these same files; that sweep is not a remote branch and cannot be merged —
if its owner lands a PR first, we rebase; conflicts in owned files resolve
toward the more complete scientific fix (our tests arbitrate).

## Shared seams (additive, delayed to milestone end)

- `tests/CMakeLists.txt` (new `sicnu_add_test` lines)
- `src/processing/CMakeLists.txt`, operator CMakeLists (new sources)
- `CHANGELOG.md` (one entry, track end)
- `data/help/*` (only when owned operator contracts change)

## Other tracks' cores (never edited here)

| Track | Files |
|---|---|
| execution-concurrency-9 | `src/workflow/**`, `src/processing/framework/**`, `src/data/data_manager.*` |
| workbench/UX tracks | `src/app/**`, `src/gui/**`, `src/ui/**`, widgets |
| cartography | `src/agent/cartography/**`, solver |
| agent/harness | `src/agent/**` (except nothing), `src/agent/harness/**` |
| geospatial data fabric | `src/geospatial/**`, io operators `src/operators/io/**` |
| dataset/experiment | `src/dataset/**`, `src/data/**`, `src/experiment/**` |
| help/diagnostics | `src/help/**`, help catalogs |
| plugin/model tracks | `src/plugins/**`, model runtime |

Observed active edits in the shared checkout (2026-09-11, uncommitted):
cartography composition/style_compiler, harness recipe_catalog/preflight,
mapspec conditions, qgis display/docks, widgets (histogram/roi/scan pool/
spectral profile), dataset split, geospatial raster_reader, help store,
io operators, fusion aliases, inference operator, sar terrain flatten +
spectral index + scientific contracts + terrain flow + task center +
workflow coordinator + sar_terrain. **Interpretation:** that sweep overlaps
our four owned files; we deliberately re-derive the scientific fixes from
the issues on our own branch with our own tests, and never copy uncommitted
edits we cannot review in a merged state.
