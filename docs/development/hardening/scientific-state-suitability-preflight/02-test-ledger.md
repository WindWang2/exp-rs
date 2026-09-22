# 02 — Test ledger (defect → fix → oracle)

Per the operator directive there was **no local build/test execution** in
this slice; each oracle's RED side is a source-level trace on
`origin/master` (a9dc33fa73), and CI provides the first GREEN run.

| # | Defect (master evidence) | Fix | Oracle (RED on master because…) | Test |
|---|---|---|---|---|
| 1 | `sicnu_preflight` + schema test outside build graph (grep: no `add_subdirectory(src/preflight)`; no registration of `test_preflight_report_schema`) | **Deduped to open PR #1246** (same add_subdirectory + test registration + wiring drift oracle). This branch initially carried the identical fix and dropped it at PR time to keep the shared central files single-owned | — (owned by #1246; independently confirmed here) | #1246's `test_build_wiring_drift` |
| 2 | `gridFacts` array-only parsing → grid checks no-op on object-shaped inspect docs (`band_facts.cpp:112-123` vs `raster_inspect_tool.cpp:222-236`) | shape-tolerant `gridFacts` (object|array, numeric-guarded) | "Raster size mismatch" / "resample to a shared grid" messages cannot be emitted from object-shaped inputs; verdict stays "ok" instead of "blocked"/"fixable" | `test_platform5` "preflight reads grid facts from inspect-shaped docs" |
| 3 | `fixable` verdict unreachable (`addWarning` hard-coded `repairable=false` while `runScientificPreflight:1215` keys fixable on repairable warnings) | `addWarning(..., repairable=false)` default; the resample warning opts in | `resample.verdict == "fixable"` assertion fails on master ("ok") | same as #2 |
| 4 | empty `temporal_facts` object bypasses min-scene checks silently (`temporalSeriesRules`: any object with no content counts as facts) | declared-but-empty / hostile `scene_count` degrades to the honest TIME_ORDER_INVALID warning; non-string dates no longer compare via `asString()` | no "no usable scene_count" message exists on master; with `{}` the pack emits nothing at all | `test_platform5` "empty temporal facts and degree-based windows degrade honestly" |
| 5 | degree-based pixel size compared against `min_resolution_meters` (`inferenceRules`) | geographic CRS (shared closed list) degrades to "cannot be verified" advice; meter comparisons need a non-geographic CRS | master emits "finer than the model's recommended minimum" for an 8.98e-5° scene and not "cannot be verified" | same as #4 |
| 6 | geographic-authid list duplicated (`workflow_analysis.cpp:44`, lowercase variant `workflow_facts.cpp:50`) plus a missing degrees guard | single `facts::isGeographicAuthid` in `band_facts` (union list incl. CRS:84); workflow_analysis delegates | structural: drift between the two lists can no longer compile-time diverge; behavior widening (CRS:84 = geographic) unpinned by any suite | compile + existing requires_projected suites stay green |
| 7 | `skip_preflight=true` executes a plan whose inputs do not resolve (`plan_tools.cpp:1050`) | gate unconditional for typed intents; flag removed from schema; intent-less custom plans keep the natural bypass | master path compiles and submits with `executed=true`; fixed path returns `success` + `executed=false` + `preflight.verdict=="blocked"` | `test_autonomy_gate` "skip_preflight can no longer bypass a blocked intent preflight" |
| 8 | NaN/inf doubles: NaN geotransform → passport serialized with `null` that its own reader rejects; `1e+9999` → +inf accepted then re-serialized non-strictly (`asset_state_resolver.cpp:833-880`, `asset_state_json.cpp readDouble`) | parse side rejects non-finite with typed InvalidField; resolver grounds nothing on a non-finite geotransform (typed unknowns + `geometry.geotransform_not_finite` note) | on master `assetStateFromJson(serializeState(state))` fails after a NaN-geotransform resolution; `pixel_size_x: 1e+9999` parses | `test_scientific_state_geo` "non-finite geotransform yields typed unknowns…"; `test_scientific_state_review` "non-finite doubles are typed InvalidField…" |
| 9 | collector cap bounded scanned entries, `dropped()==0` on >512-item files (`gdal_state_facts.cpp:24-26`) | loop scans every entry; `MetadataItems::add` owns the cap and the drop count | master yields `droppedMetadataItems==0` and no `facts.metadata_truncated` note for a 600-item GTiff | `test_scientific_state_gdal` "metadata cap drops are counted, never silent" |
| 10 | suitability extent doubles unguarded against non-finite / missing members (`dataset_facts.cpp` extent block; the module's own Slice G standard demands typed failure) | `finiteDouble` gate on `min_x/min_y/max_x/max_y` (`suitability.facts_invalid`) | master accepts `min_x: inf` and a `has_extent:true` doc with missing members (silently 0.0) | `test_suitability_adversarial` extended "unrepresentable … facts fields" case |

## Known limitations / P3 (recorded, not fixed)

- `sarFacts` calibration substring match (`"dn"` inside e.g. a hypothetical
  `"undefined"` state) — vocabulary today makes it unreachable; left alone
  to avoid loosening `sigma0*` matching.
- `PreflightOutcome::toJson` never carries `"fixable"` from blockers with
  preparation actions; blockers stay blockers by design (contract note).
- `src/preflight` remains a schema leaf: slice B (engine, ack request
  handling, budget enforcement, mirror provider) is #1207's declared future
  direction and was NOT implemented here.
- `scientific_preflight` "supervised=false" per-entry opt-out is effectively
  dead (any default-true input re-enables the training demand); semantics
  need a product decision, not a drive-by change.
- Collector fixture directory remains `CMAKE_SOURCE_DIR/build-rs14-passport`
  (pre-existing pattern; flagged as follow-up hygiene, not changed here).
