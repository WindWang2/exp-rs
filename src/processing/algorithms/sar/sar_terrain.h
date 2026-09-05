// src/processing/algorithms/sar/sar_terrain.h
// DEM-based SAR terrain correction (Platform 3.0).
//
// Scientific scope (documented limitation): this is the classic
// **plane-fit / projected-area terrain flattening** model, NOT a full
// range-Doppler terrain correction. It assumes the input and DEM are already
// co-registered on the same grid (radar geometry for GRD products) and needs
// the constant incidence angle θ0 plus platform heading. Under those
// assumptions it corrects radiometric distortion from local terrain slope:
//
//   local incidence angle (degrees):
//     cos θi = cos α · sin(90° − θ0) + sin α · cos(90° − θ0) · cos(β − φ)
//           = cos α · cos θ0 + sin α · sin θ0 · cos(β − φ)
//     with α = local slope, β = downslope aspect, φ = look azimuth (heading).
//
//   terrain-flattened gamma0 = sigma0 · cos θ0 / cos θi
//
//   layover / shadow masking: cos θi ≤ cos θmax (θi ≥ θmax, default 85°) or
//   the layover condition (downslope facing away steeper than the look) is
//   flagged as invalid in the mask band.
//
// Grids must match exactly (enforced by the operator preflight, not silently
// resampled — the temporal layer's "no hidden resampling" rule applies here).
#pragma once

#include <QString>

#include <vector>

class GdalDatasetWrapper;
class GdalStreamingOutput;

namespace sicnu::sar
{

struct TerrainCorrectionOptions
{
  double incidenceDeg = 30.0;   ///< scene incidence angle θ0 (near-range center)
  double headingDeg = 0.0;      ///< platform heading / look azimuth φ (degrees)
  double flattenCosThetaMax = 0.0871557; ///< cos(85°): θi beyond this → invalid
  bool applyFlattening = true;  ///< gamma0 = sigma0 · cosθ0/cosθi
  bool applyShadowMask = true;  ///< write the validity mask band
  bool writeIncidenceBand = false; ///< append the local incidence angle (deg)
  double demUnitScale = 1.0;    ///< DEM unit → meters (SICNU_DEM_UNIT aware)
};

/// Slope (degrees) and aspect (degrees, downslope azimuth, 0..360) per pixel
/// from one DEM tile (Horn's 3×3 method, edge-replicated borders). Pure and
/// unit-testable: `slopeAspectAt` exposes the single-pixel formula.
struct SlopeAspect
{
  double slopeDeg = 0.0;
  double aspectDeg = 0.0;
  bool valid = true;   ///< false when a DEM neighbor is NoData/NaN
};

SlopeAspect slopeAspectAt( const float *dem, int bufferWidth, int x, int y,
                           double cellSizeMeters, double demUnitScale );

/// Local incidence angle θi (degrees) from slope/aspect and geometry.
/// Returns a value in [0°, 180°]; θi > 90° means the facet faces away.
double localIncidenceAngle( double slopeDeg, double aspectDeg, double incidenceDeg,
                            double headingDeg );

/// Whether the facet is invalid (shadow or layover) given θi.
bool isLayoverOrShadow( double incidenceLocalDeg, double cosThetaMax );

/// Streaming terrain correction: sigma0 (linear) + DEM → gamma0. The output
/// band layout is: band 1 = gamma0; band 2 = Byte validity mask (1 = valid,
/// 0 = layover/shadow, 255 = nodata) when applyShadowMask; then one Float32
/// band with the local incidence angle in degrees when writeIncidenceBand
/// (so the incidence band is the LAST band, 3 or 2 depending on the mask).
/// DEM and data must share the exact grid (checked here; blocking error).
/// Returns false on I/O failure or grid mismatch (caller abandons output).
bool terrainFlattenRaster( const GdalDatasetWrapper &sigma0Ds, int band,
                           const GdalDatasetWrapper &demDs,
                           const TerrainCorrectionOptions &options, float nodata,
                           GdalStreamingOutput &dst, int tileDim,
                           const QString &polarizations = QString(),
                           const QString &sensor = QString() );

} // namespace sicnu::sar
