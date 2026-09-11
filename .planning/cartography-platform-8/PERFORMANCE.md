# PERFORMANCE / RESOURCE NOTES — cartography-platform-8

## Build/test discipline (16-core Linux host, sibling 8.0 tracks building
## concurrently; this track stayed at ≤ -j3, tests -j1)

- Configure: Ninja, Release, system QGIS/Qt6 (no vcpkg), worktree-local
  `build/`.
- Targeted builds first (`ninja test_mapspec`), harness targets built only
  when their files were touched (`test_harness_catalog`, `test_harness_evals`,
  `test_agent_tools_3`).
- All test runs `-j1` with per-test timeouts (≤ 600 s; typical case ≤ 2 s;
  the two PNG render cases ~31 s + ~1 s — real QgsLayoutExporter work).

## New hot-path costs (all bounded, all deterministic)

- **NoData apply (M1)**: one `QgsRasterRangeList` assignment + one
  `setNodataColor` per apply — O(1) on top of the existing renderer install;
  no extra passes, no allocations beyond one range list.
- **Connector compile (M2)**: one linear extent→page projection + a
  ray/box exit computation (≤ 6 flops) + one shape item per declared
  connector. No scans; catalog-wide compiles unaffected (inset loops already
  linear in item counts).
- **v5 validation (M3)**: output block ≤ 8 formats; binding checks are
  per-member type checks; matrix squareness walks rows once (≤ 24 rows).
  Cost is dominated by the pre-existing per-item walk; no measurable change
  (suite wall time unchanged: baseline 60.05 s vs post-change 44.86 s for a
  superset of tests — the P8 additions add ~9 s across 15 cases, the visual
  PNG cases dominate).
- **Page-aware solver (M4)**: one map lookup + one comparison per
  keep_with/avoid_overlap application attempt. No additional relaxation
  passes (page-overflow is a permanent Failed, which *shortens* sweeps for
  over-constrained docs).
- **Typography policies (M5)**: halfwidth compression is one extra compare
  per measured line-end; wrap pass counts unchanged. All budgets
  (kMaxWrappedLines, kMaxFitIterations) unchanged.
- **NoData legend (M6)**: preflight rule is one registry lookup per legend
  with a style_ref (already loaded); repair is one field stamp; compiler
  adds two items per declared nodata legend.
- **Compose identity (M7)**: structuralDigest is the existing 7.0 SHA-256
  over resolved geometry (id-sorted, length-framed); provenance walks the
  collections once collecting declared `source_component` refs (bounded by
  item counts). confirmMapOutput copies fields — no second implementation.

## Memory

No new persistent caches/registries. The largest new allocation is the
GeoTIFF test fixture (8×8 Float32 = 256 B + GDAL block overhead), released
with the QTemporaryDir.
