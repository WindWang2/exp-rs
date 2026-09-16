// sar_coregistration.h — local offset-field co-registration for same-grid
// complex SLC pairs (Advanced InSAR 11.0, package C; DECISIONS D-003).
//
// HONEST SCOPE: a TRANSLATION-FIELD model. Offsets are estimated on a
// patch lattice (magnitude NCC with parabolic sub-pixel refinement per
// patch, median-filtered over the lattice for speckle-outlier rejection)
// and bilinearly interpolated per pixel; the slave is resampled by that
// field. No affine/polynomial warp, no DEM-based or range-Doppler
// refinement — rs:sar_coregister remains the global single-shift special
// case, and geometry-driven warps belong to the geometric workbench
// domain. Claims beyond translation fields are defects.
//
// Pipeline (estimate → median → warp):
//   1. Global coarse model via coregistrationShift (sar_insar.h authority,
//      same NCC conventions): the fallback everywhere the lattice has no
//      confident patch, and the cross-check < 3 confident global patches
//      fails the whole estimate.
//   2. Per-patch NCC over ±searchRadius around the patch origin (NOT
//      around the global shift — the field may carry range ramps the
//      global model would clip): peak ratio below minPeakRatio or a flat
//      correlation surface marks the patch UNCONFIDENT (kept, flagged —
//      the caller sees the honest coverage, patches are not silently
//      dropped as in the global estimator).
//   3. Median filter (radius 1 = 3×3 by default, 0 disables): each
//      confident patch takes the median of its confident neighbors'
//      offsets (itself included); unconfident neighbors never contribute.
//      Even-count medians average the two middle values (the repo's
//      median convention). This removes single-patch speckle outliers;
//      deformation gradients stronger than the lattice spacing should
//      disable it (medianRadius = 0) rather than be smoothed away.
//   4. warpComplexByOffsetField: dst(x,y) = src(x + dx(x,y), y + dy(x,y))
//      — the NEGATED application of the content-displacement field, per
//      the function contract below — with dx/dy bilinearly interpolated
//      over the lattice NODE CENTERS
//      (px + patchSize/2, py + patchSize/2), edge-clamped; unconfident
//      nodes contribute the global shift. Only actually-tapped neighbors
//      are read (zero weights never touch out-of-range corners — the
//      shiftComplexBilinear edge convention); any NaN source tap makes the
//      output sample NaN.
//
// Determinism: every step is a pure function of the input planes and
// integer parameters (no RNG, no threading, fixed iteration order).
// Cancellation is cooperative through the probe, checked every 16 patches
// (same cadence as the global estimator).
#pragma once

#include "sar_insar.h"

#include <complex>
#include <functional>
#include <vector>

namespace sicnu::sar
{

/// One lattice node's estimate. Unconfident nodes keep the raw probe
/// values (NaN) and are excluded from median/warp interpolation.
struct OffsetPatch
{
    double dx = 0.0;        ///< refined x offset (pixels, slave shift)
    double dy = 0.0;        ///< refined y offset (pixels, slave shift)
    double peakRatio = 0.0; ///< best/second-best NCC score (confidence)
    bool confident = false;
};

struct OffsetField
{
    int latticeCols = 0;         ///< node columns (px lattice)
    int latticeRows = 0;         ///< node rows (py lattice)
    int patchSize = 0;
    int patchStride = 0;
    int patchOriginX0 = 0;       ///< first px (matches the global estimator loop)
    int patchOriginY0 = 0;       ///< first py
    CoregisterShift global;      ///< coarse fallback model (authority struct)
    std::vector<OffsetPatch> patches; ///< row-major, latticeRows × latticeCols
    long confidentPatches = 0;   ///< confident nodes after filtering
    long medianAdjusted = 0;     ///< nodes whose offset the median filter moved
};

/// Estimates the local offset field. @see the header for the pipeline and
/// the parameter semantics (same conventions as coregistrationShift:
/// patchSize ≤ 0 etc. are refusals; minPeakRatio is the best/second
/// confidence floor).
bool estimateOffsetField( const std::complex<float> *master,
                          const std::complex<float> *slave,
                          int w, int h, int searchRadius, int patchSize,
                          int patchStride, double minPeakRatio,
                          int medianRadius, OffsetField *out,
                          const std::function<void()> &cancelProbe = {} );

/// Per-pixel interpolated offset (bilinear over node centers, edge-clamped;
/// unconfident nodes contribute the global shift). Always finite for a
/// valid field — the field degrades to the global model, never to NaN.
void offsetAtPixel( const OffsetField &field, double x, double y,
                    double *dx, double *dy );

/// Resamples @a slave by the local offset field into @a dst (both w*h,
/// row-major complex planes): dst(x,y) = src(x+dx(x,y), y+dy(x,y)) — the
/// negated application of the content-displacement field, matching
/// rs:sar_coregister's aligning convention (the NCC offsets say where the
/// slave content CAME FROM, so aligning samples at +offset). NaN wherever
/// the tapped source is NaN or out of range (masked, never clamped).
void warpComplexByOffsetField( const std::complex<float> *slave, int w, int h,
                               const OffsetField &field,
                               std::complex<float> *dst,
                               const std::function<void()> &cancelProbe = {} );

} // namespace sicnu::sar
