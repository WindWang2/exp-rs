// src/processing/algorithms/temporal/temporal_fusion.h
// Optical+SAR temporal feature-fusion contract (Temporal Phenology 12.0,
// WP6).
//
// Scope: feature-level fusion of ALREADY-COREGISTERED rasters — typically a
// stack of optical temporal features (phenology metrics, trend coefficients)
// plus a stack of SAR temporal features (rs:sar_temporal_stats backscatter
// summaries, coherence statistics). The contract verifies that both inputs
// genuinely share one pixel grid BEFORE concatenating features, and emits a
// provenance record describing what was fused.
//
// Deliberately out of scope: coregistration, resampling, warping. A grid
// mismatch is a typed failure, never a silent realign — "registration" and
// "joint identification" stay separate concerns.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace sicnu::temporal
{

/// The pixel-grid identity of one raster band stack.
struct GridSignature
{
  int width = 0;
  int height = 0;
  /// GDAL affine geotransform [originX, pixelW, rotX, originY, rotY, pixelH].
  std::array<double, 6> geoTransform = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  /// WKT (or authority string) — compared textually after trimming; two empty
  /// projections are considered equal (both unprojected).
  std::string projection;
};

struct GridCompatibility
{
  bool compatible = false;
  /// Typed mismatch codes, stable for machine consumers:
  ///  "width" | "height" | "geotransform" | "projection"
  /// Empty when compatible == true.
  std::vector<std::string> mismatches;
};

/// Shared-grid check between two candidate fusion inputs.
/// @a gtol is the absolute tolerance applied per geotransform coefficient
/// (caller picks: sub-pixel shifts are usually fatal for feature fusion, so
/// the default expectation is exact-to-epsilon equality).
GridCompatibility checkGridCompatibility( const GridSignature &a,
                                          const GridSignature &b,
                                          double geotransformTolerance );

/// Provenance record the operator writes into output dataset metadata.
/// Structured, flat-key form so GDAL metadata can carry it losslessly.
struct FusionProvenance
{
  std::string opticalPath;
  std::string sarPath;
  int opticalBandCount = 0;
  int sarBandCount = 0;
  /// Band-name prefix applied to each input's bands in the output stack.
  std::string opticalPrefix = "opt_";
  std::string sarPrefix = "sar_";
  double geotransformTolerance = 0.0;
};

/// Flat "KEY=VALUE" lines for GDALSetMetadata / JSON result fields.
std::vector<std::pair<std::string, std::string>>
fusionProvenanceItems( const FusionProvenance &prov );

} // namespace sicnu::temporal
