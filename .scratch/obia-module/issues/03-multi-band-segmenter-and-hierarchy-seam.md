# 03 — Multi-Band Segmenter Deepening & Hierarchy Fallback Seams

Type: prototype
Status: resolved
Blocked by: 01

## Question

`RsSimpleSegmenter` currently averages multi-band imagery into a single mean intensity band before Gaussian smoothing and region merging, while `RsObjectHierarchy` requires external OTB CLI for multi-level segmentation.

How can `RsSimpleSegmenter` be upgraded to compute multi-spectral Euclidean/spectral-angle distances across all bands natively in pure C++, and how can `RsObjectHierarchy` provide robust multi-scale parent-child linking (`RsPixelMajorityParentLink`) and feature propagation when external OTB tools are not available?

## Answer

1. Deepened `RsSimpleSegmenter::segmentMultiBand` in `src/analysis/segmentation/rs_simple_segmenter.cpp` to perform individual Gaussian smoothing across all bands, followed by multi-spectral tuple quantization and composite integer key hashing.
2. Preserved multi-spectral band contrast during 8-connected flood fill component labeling and region merging.
3. Ensured `RsPixelMajorityParentLink` and `RsObjectHierarchy` function smoothly with the enhanced multi-band segmenter fallback.

