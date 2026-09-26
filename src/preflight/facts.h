// facts.h — typed input facts for the Scientific Preflight Engine (RS14-02).
//
// Slice B: facts never enter rules as prose or raw operator structures; they
// enter through the provider seam (provider.h) as this typed projection.
// Every field is optional-by-default: a missing fact is a typed unknown the
// rules must report, never a silently assumed value.

#pragma once

#include <string>
#include <vector>

namespace sicnu::preflight {

/// Availability of one consulted fact bundle.
///   Available   — the authority resolved the subject; fields are meaningful.
///   Unknown     — the authority has no record for this subject (typed unknown).
///   Unavailable — the authority itself could not be consulted (detail says why).
enum class FactStatus
{
  Available,
  Unknown,
  Unavailable,
};

const char *factStatusToString( FactStatus status );

/// One band of a raster slot, projected from the scientific-state passport.
struct BandFacts
{
  int index = 0;  ///< 1-based GDAL band index; 0 = undeclared.
  std::string role;  ///< Band-role vocabulary ("red", "nir", ...); "" = unknown.
  bool hasWavelengthNm = false;
  double wavelengthNm = 0.0;
  std::string dataType;
};

/// Resolved facts for one requested input slot. Defaults are "unknown", so a
/// half-populated projection degrades to typed unknowns rather than fake
/// confidence. Bounded: providers cap band/derived/date counts and say so.
struct SlotFacts
{
  std::string slot;       ///< Slot name from the request (engine-stamped).
  std::string assetRef;   ///< The reference as requested.
  std::string assetId;    ///< Catalog identity, empty when unknown.
  std::string kind;       ///< Asset-kind vocabulary ("raster", "vector", "model", ...).
  std::string modality;   ///< optical | sar | thermal | hyperspectral | unknown.

  // Geometry.
  bool hasCrs = false;
  std::string crsAuthid;  ///< e.g. "EPSG:32650"; empty when unnamed.
  std::string crsWkt;     ///< Fallback comparison when no authid.
  bool crsProjected = false;
  bool hasPixelSize = false;
  double pixelSizeX = 0.0;
  double pixelSizeY = 0.0;
  bool hasSize = false;
  int width = 0;
  int height = 0;
  // Declared extent in the CRS of the asset (authority-projected; rules only
  // compare it when both sides are present — never an implicit mismatch).
  bool hasExtent = false;
  double extentMinX = 0.0;
  double extentMinY = 0.0;
  double extentMaxX = 0.0;
  double extentMaxY = 0.0;

  // Radiometric state (normalized unit vocabulary of the passport).
  std::string radiometricUnit;

  // Validity / quality.
  std::string noDataPolicy;  ///< declared | undeclared | partial | "".
  bool hasCloudCover = false;
  double cloudCoverPercent = 0.0;
  std::string qualityMaskInfo;  ///< e.g. "s2_scl"; empty = none declared.

  // Bands (sorted by 1-based index).
  std::vector<BandFacts> bands;

  // Acquisition / temporal.
  bool hasAcquisitionTime = false;
  std::string acquisitionTimeIso;  ///< ISO-8601; empty when unknown.
  int temporalSceneCount = 0;      ///< Declared collection scene count; 0 = none.
  std::vector<std::string> temporalDates;  ///< ISO dates, ascending or not — rules judge.
  bool temporalTruncated = false;  ///< Provider dropped dates beyond its cap.
  /// Scenes in the declared collection whose acquisition time is missing or
  /// unparseable. 0 = every declared scene resolved. A provider that drops
  /// such scenes from temporalDates must count them here — silently narrowing
  /// the series to the parseable scenes would fake confidence.
  int temporalInvalidTimeCount = 0;
  /// Temporal collection identities the authority attached to this asset
  /// (passport temporalRefs, sorted by collection id). Facts, not policies:
  /// a temporal provider resolves them into counts/dates or typed unknowns.
  std::vector<std::string> temporalCollectionRefs;

  // Leakage-relevant identity: assets this one was derived from.
  std::vector<std::string> derivedFromAssetIds;

  // Model manifest (projected from the model sidecar / catalog).
  bool hasModelManifest = false;
  std::string modelKind;  ///< Declared model family/kind, empty when unknown.
};

} // namespace sicnu::preflight
