# ADR 0073: Large-Raster Memory Policy Classification

## Context

The mission requires that "the system must not claim large-raster support
based only on a few streaming algorithms": every major raster operator should
clearly declare a memory policy (Streaming / MultiPassStreaming / FullRaster /
ExternalProcess / UnsupportedForLargeRaster). Previously the policy was
implicit in each implementation — `GdalBlockStream` was used only by
radiometric calibration and atmospheric correction; the rest loaded full
rasters or delegated to CLIs, with no declared contract.

## Decision

1. **`RSOperatorMemoryPolicy`** (`rs_operator.h`, namespace
   `sicnu::operators`): `Streaming` (O(tile) out-of-core), `MultiPassStreaming`
   (multiple streaming passes + small global state), `FullRaster`
   (O(width×height×bands)), `ExternalProcess` (CLI manages its own tiling),
   `UnsupportedForLargeRaster`. Stable lowercase ids via `memoryPolicyName()`.
   `RSOperator::memoryPolicy()` defaults to `FullRaster`; operators override.

2. **Descriptor surface**: `AgentMetadata::memoryPolicy` is parsed/written from
   `memoryPolicy` JSON, and `AlgorithmDescriptorBuilder::buildFromRsOperator`
   injects every operator's policy into its agent metadata — so UI, Agent/MCP,
   and the toolbox all see the declared policy.

3. **Classification (2026-08-07 audit)**:
   - `Streaming`: `rs:radiometric_calibration` (GdalBlockStream); `gdal:orthorectification`,
     `gdal:reproject`, `gdal:clip`, `gdal:polygonize` (in-process GDAL manages tiling).
   - `MultiPassStreaming`: `rs:atmospheric_correction` (DOS1/DOS2: range →
     histogram → apply; QUAC remains full-raster by design).
   - `ExternalProcess`: `otb:meanshift_segmentation`, `otb:svm_classification`,
     `otb:compute_images_statistics` (otbcli_*).
   - `FullRaster` (default): spectral index, band math, change detection
     (incl. CVA), QA mask, image fusion, PCA, SAM, KMeans, supervised/OBIA
     classification, terrain, mosaic, continuum removal, segment stats,
     `rs:infer`, all `opencv:*` filters.
   - `UnsupportedForLargeRaster`: none currently declared.

## Consequences

- Large-image behavior is explicit and machine-readable per operator rather
  than implied by which kernels happen to stream.
- The audit is a living contract: any operator that gains streaming must
  override its policy; a test walks the registry and asserts every operator
  declares a valid policy.
- Memory-hungry operators (PCA, fusion, classification) are honestly labeled
  FullRaster, feeding the follow-up streaming-remediation backlog rather than
  overstating capability.
