# ADR 0060: Converge the Segmentation Operator Stack onto the Analysis Layer

## Status
Accepted

## Context
Dual teaching segmenters: `segutil::segmentQuantize` (operators, cv::Mat, no nodata, weaker merge) vs `RsSimpleSegmenter` (analysis, RsSegmentMap, nodata-aware). The majority tie-break rule (max votes, ties → smaller id) is re-implemented four times — `rs_parent_link.cpp` P1, `labelFromRoi` (hierarchy operator, point-in-polygon), the classify votes loop (ALL_TOUCHED rasterize), and an inline test copy.
`writeLabelGeoTiff` duplicates `RsSegmentMap::toGeoTIFF` (ADR 0054); `writeClassGeoTiff` is orphaned since ADR 0055.

## Decision
1. **One teaching segmenter**: `rs:obia_segment`/`rs:obia_classify` (quantize) delegate to `RsSimpleSegmenter::segmentMultiBand`, which gains optional `isCanceled`/`onProgress` hooks (GUI caller source-compatible); labels write via `RsSegmentMap::toGeoTIFF`; nodata = band-1 declared value, else NaN; label 0 = nodata.
2. **One majority kernel**: `majorityKeyWithTieBreak` (`rs_majority_vote.h`) — P1, `RsRoiLabeler`, and the operator decisions delegate; vote-collecting loops stay per-site.
3. **One ROI labeler**: `RsRoiLabeler::labelByMajority` — canonical membership is center-of-pixel rasterize via the existing `RsPixelRasterizer` (shared with training extraction, windowed allocation, matches the hierarchy path's pixel-center semantics); retires point-in-polygon and ALL_TOUCHED (double-counted boundaries).
4. **Deletions**: segutil loses `segmentQuantize`/`mergeSmallRegions`/`writeLabelGeoTiff`/`writeClassGeoTiff`/`rasterizeGeometry` (keeps `segmentGrid`); `labelFromRoi` deleted; inline test copy → kernel tests.
5. **Smoke**: `rs:obia_hierarchy` gains registration + schema coverage and an OTB-gated execution smoke (OtbError fail-closed without OTB).

## Consequences
- **Quantize-path output changes** (blur kernel, merge algorithm, id assignment) — accepted; no test pins old ids/counts beyond `segments >= 1`.
- **Classify ROI labels converge to center-of-pixel** boundary semantics; the hierarchy path keeps pixel-center semantics via rasterize (gains windowed allocation + shared field fallback).
- **One `RsSegmentMap` stack for teaching segmentation**; segutil shrinks to `segmentGrid`.
