// src/processing/algorithms/temporal/temporal_collection.h
// TemporalCollection — the lightweight multi-temporal data abstraction.
//
// A collection is METADATA + REFERENCES: every scene points at an existing
// raster asset (file path today, optional DataManager asset id/revision for
// asset-bound callers). No pixels are copied, cached, or materialized here.
// The descriptor is a small versioned JSON document that can be saved and
// reloaded with ordering and time metadata preserved.
#pragma once

#include "temporal_time.h"

#include <json/json.h>

#include <QString>
#include <QStringList>
#include <QVector>

#include <map>

namespace sicnu::temporal
{

/// How duplicate acquisition instants are handled. Deterministic either way:
/// KeepAll retains every scene and breaks ties by original input order;
/// Reject turns duplicates into a blocking preflight error.
enum class DuplicatePolicy
{
  KeepAll,
  Reject
};

/// One scene (= one date) inside a collection. Reference, never a copy.
struct TemporalSceneRef
{
  QString path;                ///< raster file path (the pixel owner)
  QString assetId;             ///< optional DataManager asset binding (informational)
  QString assetRevision;       ///< optional DataManager revision snapshot
  AcquisitionTime time;        ///< acquisition time (valid required by preflight)
  QString timeSource;          ///< "explicit" | "metadata" | "filename" | "descriptor"
  QString platform;            ///< SICNU_SPACECRAFT metadata, may be empty
  QString processingLevel;     ///< SICNU_PROCESSING_LEVEL metadata, may be empty
  /// Per-scene explicit 1-based band overrides: role id ("nir") → band.
  std::map<QString, int> bandOverrides;
  int qualityBand = 0;         ///< optional quality-score band (higher = better; 0 = none)
  int maskBand = 0;            ///< optional QA/cloud mask band (1-based; 0 = auto-detect)
  int originalIndex = 0;       ///< input order — deterministic tie-break

  // --- Multimodal observation contract (goal §11, ADR 0125 forward seam) --
  // Optional, additive descriptors so a future SpatioTemporalCollection can
  // carry Optical / SAR / DEM / Auxiliary scenes under ONE identity without a
  // new inheritance hierarchy. Nothing in the temporal layer branches on
  // these yet: they round-trip through the descriptor, STAC ingestion can
  // populate them, and preflight/agents may surface them. Empty = unclaimed.
  QString modality;            ///< "optical" | "sar" | "dem" | "auxiliary" (empty = optical/unknown)
  QString sensor;              ///< Finer than platform, e.g. "ETM+", "C-SAR" (empty = unknown)
  QStringList bandRoles;       ///< Declared per-band role ids ("nir","red","vv","vh",...)
  QStringList polarizations;   ///< SAR polarizations ("VV","VH","HH","HV"); empty = n/a
  double resolutionMeters = 0.0;   ///< Nominal spatial resolution; 0 = unknown
  QString radiometricState;    ///< "dn" | "toa_reflectance" | "boa_reflectance" | ... (empty = unknown)
  double cloudCoverPercent = -1.0; ///< Scene quality [0,100]; <0 = unreported

  Json::Value toJson() const;
  static TemporalSceneRef fromJson( const Json::Value &v, QString *error );
};

class TemporalCollection
{
public:
  /// Assembles a collection from scene paths. Times are resolved per scene:
  /// explicit entry in @a explicitTimes wins, then SICNU_ACQUISITION_DATE
  /// product metadata, then a conservative filename parse. Scenes whose time
  /// cannot be determined are kept with time.valid == false — preflight turns
  /// that into a blocking issue (missing time is never silently guessed).
  static TemporalCollection fromScenePaths( const QStringList &paths,
                                            const QStringList &explicitTimes = {},
                                            const QString &name = {} );

  /// Sorts scenes chronologically (instant, then original input order) so the
  /// ordering is deterministic even with duplicate timestamps.
  void sortScenes();

  /// Removes and reports duplicate instants per @a policy. Returns the number
  /// of dropped scenes (0 for KeepAll).
  int applyDuplicatePolicy( DuplicatePolicy policy, QStringList *droppedPaths = nullptr );

  // --- JSON descriptor (save / reload) ---
  Json::Value toJson() const;
  /// Parses a descriptor; returns !ok and fills @a error on mismatched schema.
  static bool fromJson( const Json::Value &v, TemporalCollection *out, QString *error );
  bool save( const QString &filePath ) const;
  static bool load( const QString &filePath, TemporalCollection *out, QString *error );

  // --- accessors ---
  const QVector<TemporalSceneRef> &scenes() const { return m_scenes; }
  QVector<TemporalSceneRef> &scenes() { return m_scenes; }
  QString name() const { return m_name; }
  void setName( const QString &name ) { m_name = name; }
  DuplicatePolicy duplicatePolicy() const { return m_duplicatePolicy; }
  void setDuplicatePolicy( DuplicatePolicy p ) { m_duplicatePolicy = p; }

  /// Valid time range (ISO strings); empty when no scene has a valid time.
  QString timeRangeStartIso() const;
  QString timeRangeEndIso() const;
  int sceneCount() const { return m_scenes.size(); }

private:
  QString m_name;
  QVector<TemporalSceneRef> m_scenes;
  DuplicatePolicy m_duplicatePolicy = DuplicatePolicy::KeepAll;
};

/// Inspects one raster file and fills a scene ref (metadata + time resolution).
/// Returns false + @a error when the file cannot be opened as a raster.
bool inspectScene( const QString &path, const QString &explicitTime,
                   TemporalSceneRef *out, QString *error );

/// Parses a duplicate-policy token ("keep_all" | "reject").
DuplicatePolicy duplicatePolicyFromString( const QString &token, bool *ok );

} // namespace sicnu::temporal
