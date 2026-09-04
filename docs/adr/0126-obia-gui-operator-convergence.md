# ADR 0126: Converge the OBIA GUI onto the rs:obia_* Operator Seam

## Status
Accepted

## Context
Issue #663: the OBIA window executed analysis kernels through TaskCenter
executor lambdas under pseudo ids (`module:obia:segment`, `module:obia:level_features`,
`module:obia:hierarchy`, `module:obia:hierarchy_classify`, `module:obia:classify`) —
jobs that enjoyed TaskCenter admission/progress/cancel but never instantiated an
`RSOperator`. The kernels (RsOtbSegmenter, RsSimpleSegmenter, RsSegmentFeatures,
RsObjectClassify, RsAccuracyAssessment, RsClassRaster) were called directly from
`src/app/obia`, with an app-layer OTB-first/fallback policy (ADR 0058) and a
hand-rolled rasterize-and-vote ROI importer that duplicated RsRoiLabeler
(ADR 0060). `RsObiaTask` (a QgsTask wrapper) reimplemented the
segment→features→classify pipeline in-app, and `mosaic_panel` carried a
`callable:mosaic_panel` lambda duplicating `rs:mosaic`.

A prior perf-branch review of #663 objected that a naive swap to
`rs:obia_segment` (the teaching segmenter) would downgrade the GUI's OTB
MeanShift kernel and force the in-memory RsSegmentMap through a file seam.

## Decision
1. **Operators absorb the GUI's capabilities — nothing is downgraded**:
   - `rs:obia_segment` gains `engine` = `simple` (default, unchanged) |
     `otb` (MeanShift via RsOtbSegmenter, fail-closed) | `auto` (the ADR 0058
     prefer-OTB/teaching-fallback policy, now owned by the operator), plus the
     OTB parameters (`spatialRadius`, `rangeRadius`, `maxIterations`,
     `threshold`) and validation parity with `rs:obia_classify`.
   - New `rs:obia_features`: full RsSegmentFeatures statistics (spectral +
     GLCM + shape) over a label raster, CSV interchange with 17-digit
     round-trip doubles.
   - New `rs:obia_label`: RsRoiLabeler majority labeling to CSV.
   - `rs:obia_classify` gains `labels` (label-raster input; skips internal
     segmentation), `segmentClasses` ({segmentId: classId} interactive
     training, XOR `training`), methods `random_forest`/`kmeans`/`mlp` +
     hyperparameters, `features=full` + `featureSelection`, `classColors`
     palette, `outputUncertainty` CSV (entropy + predicted class), and
     training accuracy in the result. Classification core unified on
     RsObjectClassify; the writer is RsClassRaster::paint (palette + dtype
     escalation + atomic rename; supersedes the bare saveLabelRaster call).
   - `rs:obia_hierarchy` gains rehydrate inputs (`labelsFine`/`labelsCoarse`/
     `parents` — its own outputs round-trip) so classify iterations reuse a
     built hierarchy without re-running OTB, plus `segmentClasses`,
     hyperparameters, palette, uncertainty and accuracy.
2. **The GUI is a thin client**. `RsObiaMainWindow` submits real operator ids
   through `TaskCenter::submitJob(req, {}, {}, autoLoad=false)` (registry
   resolution, ADR 0062). A pure adapter (`rs_obia_operator_adapter`) owns the
   WHAT: param builders + CSV/JSON rehydration. Presentation state
   (mSegMap/mSegStats/mSegmentLabels/mHierarchy) is cache rehydrated from
   operator FILE outputs — the OTB path already paid the file round-trip, and
   the teaching path's extra GeoTIFF write is negligible against segmentation
   cost (verified: full chain on 16×16–512×512 rasters completes in the same
   order as the old in-app path). The boundary rule: the GUI may hold
   label-map DATA (RsSegmentMap/fromGeoTIFF, SegmentStat, RsObjectHierarchy
   via setLevels) but may not EXECUTE analysis kernels.
3. **One source of truth for defaults**: toolbar widgets initialize from the
   operator schemas; classifier hyperparameter defaults are
   RsClassifierBackendParams (a new factory overload constructs backends with
   hyperparameters — the GUI no longer builds backends). A dedicated
   rangeRadius spin replaces the `binsSpin*0.5` estimate (16.0-vs-15.0 drift);
   the rfMinSampleCount/rfMaxDepth GUI defaults align to the backend defaults
   (10/5, previously 20/1).
4. **Deletions**: `RsObiaTask`, `RsObiaSegmentation` (policy moved into
   `rs:obia_segment`), the hand-rolled ROI vote loop, the
   `callable:mosaic_panel` kernel duplication (panel → `rs:mosaic`, same
   contract as MosaicDialog), and all `module:obia:*` GUI lambdas.
5. **Tests mirror the layering**: operator contracts in test_obia_operators
   (engine matrix, CSV parity vs kernels, interactive classify, rehydrate
   without OTB, validation), the seam in test_obia_task_center (registry
   resolution, failure surfacing, cancel semantics), and thin GUI adapters in
   test_obia_main_window (dispatched ids, param mapping, schema-driven
   defaults). Direct-kernel GUI tests are gone with the path they pinned.

## Consequences
- OBIA segmentation/features/classification/labeling/export each have exactly
  one execution path: GUI, CLI, MCP, Agent and workflow invoke the same
  operator contracts (the GUI chain `rs:obia_segment → rs:obia_features` is
  the same composition `data/pipelines/obia_*.json` expresses).
- `segmentClasses` and the rehydrate hierarchy inputs are agent-usable
  (interactive OBIA sessions can be scripted); accuracy + uncertainty
  sidecars are machine-readable beyond the GUI.
- Behavior deltas, all deliberate: classify class-map writer is
  RsClassRaster::paint (palette; >65535 errors instead of Int32 escalation);
  hierarchy classify now requires ≥2 distinct classes (parity with
  rs:obia_classify); RF hyperparameter GUI defaults 100/10/5; ROI import no
  longer reports a skippedSparse count (RsRoiLabeler does not expose it) and
  runs async off the GUI thread.
- Deferred (documented, not blocking): a consolidation operator
  (RsHierarchyClassConsolidator stays interactive-only), the pixel
  classification window's in-memory session (ADR 0019 GUI parity), and the
  post-process chain (sieve/clump operators missing).
