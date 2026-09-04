// src/processing/features/feature_cube.h
// Multimodal Feature Cube contract (Platform 3.0, goal §8).
//
// A feature cube is a regular raster where every band is a FEATURE with a
// declared semantic identity: id, semantic role, unit, scale/offset, source
// asset, modality, and time semantics. Features may come from optical bands /
// indices, SAR backscatter or texture, DEM derivatives, temporal metrics, or
// model-derived products — the cube is their model-ready, self-describing
// carrier.
//
// Persistence: dataset-level metadata `SICNU_FEATURE_CUBE` holds the contract
// JSON (capped; oversized contracts spill to a `<file>.features.json` sidecar
// and the dataset item holds a pointer). Per-band items SICNU_FEATURE_ID /
// SICNU_FEATURE_ROLE / SICNU_FEATURE_UNIT ride for quick introspection.
//
// The cube contract is DATA, not an object model: operators read/write plain
// rasters and attach/parse the contract, so every consumer (rs:infer model
// matching, agent inspection, classification) sees one consistent feature
// identity without a parallel asset type.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <json/json.h>

class GdalDatasetWrapper;

namespace sicnu::features
{

/// Time semantics of one feature band: a single acquisition instant, a range
/// (e.g. a seasonal composite window), or none (static feature like slope).
struct FeatureTime
{
  QString kind;    ///< "none" | "single" | "range"
  QString startIso;
  QString endIso;
};

struct FeatureBand
{
  QString id;             ///< unique feature id inside the cube ("ndvi", "vv_logratio"...)
  QString semanticRole;   ///< role vocabulary id ("ndvi", "vv", "elevation", "contrast"...)
  QString unit;           ///< "index" | "reflectance" | "db" | "meters" | ... ("": unitless)
  double scale = 1.0;     ///< physical = stored · scale + offset
  double offset = 0.0;
  int band = 0;           ///< 1-based band in the cube raster
  QString sourcePath;     ///< producing raster (informational provenance)
  QString sourceAssetId;  ///< DataManager asset when bound
  QString modality;       ///< "optical" | "sar" | "dem" | "auxiliary" | "model_derived" | ""
  FeatureTime time;
  double nodata = std::numeric_limits<double>::quiet_NaN(); ///< NaN = undeclared
};

struct FeatureCubeContract
{
  int version = 1;
  QString featureId;      ///< cube identity ("s2_sar_dem_crop_features")
  QString generator;      ///< producing operator id
  QVector<FeatureBand> bands;
  /// Normalization stats attached by rs:feature_normalize (train/inference
  /// consistency: the stats ride with the file).
  Json::Value normalization; ///< {"method": "...", ...} or null

  Json::Value toJson() const;
  static bool fromJson( const Json::Value &v, FeatureCubeContract *out, QString *error );
};

/// Writes the contract onto an open GDAL dataset (dataset handle, void* =
/// GDALDatasetH): dataset item + per-band items. Returns false when the
/// serialized contract exceeds the metadata size cap and no sidecar path is
/// given (the caller should then pass a sidecar path).
bool writeFeatureCubeMetadata( void *datasetHandle, const FeatureCubeContract &contract,
                               const QString &sidecarPath = QString() );

/// Reads the contract from a raster (dataset item, falling back to the
/// conventional `<file>.features.json` sidecar). Returns false when the
/// raster carries no valid cube contract.
bool readFeatureCubeMetadata( const QString &rasterPath, FeatureCubeContract *out,
                              QString *error = nullptr );

/// Convenience: true when the raster carries a feature cube contract.
bool isFeatureCube( const QString &rasterPath );

/// Matching verdicts between a feature cube and a model input contract
/// (manifest v2/v3, model_catalog.h) — used by rs:infer preflight and agent
/// model ranking. Errors are human-readable and non-fatal.
struct ModelInputMatch
{
  bool ok = false;
  int missingRoles = 0;      ///< contract bandRoles not present in the cube
  int bandCountDelta = 0;    ///< cube band count − contract expected bands (0 when dynamic)
  QStringList problems;      ///< human-readable problems (empty when ok)
};

ModelInputMatch matchesModelInput( const FeatureCubeContract &cube,
                                   const QStringList &requiredBandRoles,
                                   int expectedBandCount,  ///< 0 = dynamic
                                   const QString &expectedModality = QString() );

} // namespace sicnu::features
