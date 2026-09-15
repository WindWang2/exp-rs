# ADR 0163: Large-Scale Mosaic, Fusion & Quality Composite — seamline, balancing, provenance

Status: accepted · Branch `zcode/mosaic-fusion-11` · Baseline `origin/master@a5b11b7f10`

## Context

`rs:mosaic` (production) streams a first/last-valid window merge with grid
guards; `Mosaic::merge` is the legacy in-memory reference. Pan-sharpening
kernels (GS/Brovey/IHS/HPF) and Wald memory metrics exist since ADR 0159.
What production multi-scene mosaicking still lacks: scene/grid planning with a
mixed-CRS diagnostic, inter-scene radiometric balancing, seamline placement,
seam blending, quality-score compositing with per-pixel provenance, and a
fusion quality artifact. Teaching/production runs need honest, deterministic
alternatives to "later input silently overwrites".

## Decision

1. **New additive operator `rs:quality_mosaic`**; `rs:mosaic` keeps its
   contract untouched (D-002). Same registration seams
   (rs_operators_init / capability catalog / scientific contract).
2. **Algorithm layer in `rs::mosaic` under `src/processing/algorithms/`**:
   `mosaic_plan` (A), `mosaic_balancing` (B), `mosaic_seamline` (C),
   `mosaic_blend` (D), `mosaic_quality` (E); fusion quality in
   `rs::fusion::fusion_quality_report` (F). Pure metadata/pixel kernels, no
   file I/O except the report writer.
3. **Plan**: deterministic reference grid (highest priority → lowest index),
   union extent, per-scene placement with sub-pixel flag, composite order,
   pairwise overlap inventory. Mixed CRS / rotated / pixel-size mismatch are
   *diagnostics*; the operator fails closed with an actionable message
   pointing at reprojection (D-004 — no inline reprojection; compose with
   the existing gdal reproject operator).
4. **Balancing**: overlap-graph BFS from a reference scene; per pair a
   three-pass streaming robust fit (LSQ → residual-MAD trim → inlier refit);
   cumulative gain clamps `[0.5, 2]`, bias gate in MAD-sigma units;
   `rejectPolicy=fail|drop`. No absolute calibration here — that remains the
   radiometric-calibration domain (dedupe vs PR #1008).
5. **Seamline**: per-pair cost = radiometric |Δ| + gradient disagreement +
   cloud penalty + edge-distance pull; DP min-cost path (3-connected,
   deterministic tie-break to the smaller index); binned builder bounds the
   DP to ≤512×512 cells so memory follows the seam band, not the scene (D-006).
6. **Blending**: feather ramp with weights summing to 1 and per-pixel NoData
   fallback (no cracks, no double-boundary); multiband (Laplacian) blending
   ships as a tested windowed kernel for callers that need it (D-007).
7. **Quality composite**: per-scene 0..1 score (cloud/quality/time/view,
   weights renormalized over provided dimensions, out-of-range clamped and
   flagged); paint order worst→best; provenance band records the dominant
   source per pixel (0 = unfilled).
8. **Output**: temp-path GTiff (TILED, DEFLATE, 512² blocks) published by
   rename; failure paths remove the temp; optional AVERAGE overviews degrade
   to warnings; JSON report (`exp-rs/quality-mosaic-report@1`) written
   atomically (D-009).
9. **Fusion quality**: streaming accumulator (moments + per-band Q/RASE) with
   a distortion guard (ERGAS/CC/Q/mean-ratio/std-ratio thresholds) and a
   fixed-schema JSON artifact; `rs:image_fusion` gains `hpf` and an optional
   `qualityReport` (fused vs resampled-MS fidelity, documented as *not* the
   full Wald degraded pass — use the kernel with degraded inputs for that).

## Consequences

- `rs:mosaic` remains byte-compatible; the new operator is the only consumer
  of the new algorithm layer (plus its tests).
- Deterministic outputs for identical inputs (DP tie-breaks, order rules,
  stable sorts).
- Provenance is Float32 (exact ≤ 2²⁴ scenes) — documented limitation.
- The GUI dialogs are untouched; agent/CLI surface via the operator registry.

## Alternatives considered

- Global min-cut seamlines (rejected: heavyweight, no existing graph-cut dep).
- Inline reprojection inside the mosaic operator (rejected: second authority
  for a capability that exists as `gdal` reproject operators).
- Extending `rs:mosaic` in place (rejected: mutates a locked public contract).
