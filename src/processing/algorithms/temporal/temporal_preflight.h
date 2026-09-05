// src/processing/algorithms/temporal/temporal_preflight.h
// Temporal Preflight — the scientific gate every time-series algorithm runs
// BEFORE any pixel math (goal §9). Checks, all with stable issue codes:
//
//   Time        temporal.missing_time / temporal.duplicate_time /
//               temporal.unsorted_time (internal invariant)
//   Spatial     temporal.grid_mismatch / temporal.dimension_mismatch
//               (via data::compareGrids — CRS, pixel size, sub-pixel origin,
//               extent; NO hidden resampling: mismatches must be fixed with an
//               explicit upstream warp/align step)
//   Spectral    temporal.band_role_missing
//   Radiometric temporal.radiometric_mismatch (SICNU_RADIOMETRIC_STATE),
//               temporal.scale_offset_mismatch (GDAL band scale/offset must be
//               uniformly declared and identical across scenes — never mixed)
//   Validity    temporal.nodata_undeclared (warning), temporal.mask_missing
//               (warning: masking requested but a scene carries no QA/SCL band)
#pragma once

#include "temporal_collection.h"

#include <json/json.h>

#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

class GdalDatasetWrapper;

namespace sicnu::temporal
{

struct PreflightIssue
{
  QString code;
  QString message;
  bool blocking = true;
  QString scenePath; ///< offender, when a single scene is responsible
};

struct PreflightOptions
{
  /// Role ids every scene must resolve (e.g. "red", "nir").
  QStringList requiredBandRoles;
  bool requireTimes = true;
  bool requireSameGrid = true;
  bool requireRadiometricConsistency = true;
  bool requireUniformScaleOffset = true;
  /// Detect QA/SCL bands for masking; warns when absent (non-blocking).
  bool expectQualityBands = false;
  /// Platform 3.0 modality gates: a temporal FOLD across modalities (optical
  /// + SAR in one time series) is a scientific error, not a convenience.
  /// Explicitly multimodal consumers (feature stacking) turn this off.
  bool requireUniformModality = true;
  /// SAR collections: mixing polarizations (VV vs VH) in one series is a
  /// blocking error unless the caller explicitly declares the series is
  /// polarization-aware (set true only for operators that track polarization
  /// per scene).
  bool allowMixedPolarization = false;
};

/// Modality facts gathered from the scene contracts (Platform 3.0, goal §5).
struct ModalityProfile
{
  /// Distinct claimed modalities in vocabulary order; "unknown" only when
  /// nothing else was claimed anywhere.
  QStringList modalities;
  bool mixed = false;                 ///< more than one distinct modality
  /// Normalized polarization set shared by every SAR scene ("" when none /
  /// not uniform).
  QString commonPolarizationSet;
  bool polarizationUniform = true;
  /// Some SAR scenes declare polarizations, others do not.
  bool polarizationPartial = false;
  int sarSceneCount = 0;
  int demSceneCount = 0;
};

/// Per-scene radiometric facts gathered during preflight (reused by the
/// streaming reader so the metadata pass happens exactly once per execution).
struct SceneRadiometry
{
  QString radiometricState;             ///< SICNU_RADIOMETRIC_STATE ("" unknown)
  bool scaleDefined = false;            ///< GDAL scale declared on the analysis band
  double scale = 1.0;
  bool offsetDefined = false;           ///< GDAL offset declared on the analysis band
  double offset = 0.0;
  int maskBand = 0;                     ///< resolved QA/SCL band (explicit > role)
  QString maskKind;                     ///< "landsat_qa_pixel" | "sentinel2_scl" | "explicit" | ""
};

struct TemporalPreflightReport
{
  QVector<PreflightIssue> issues;

  int sceneCount = 0;
  int scenesWithTime = 0;
  int duplicateTimeCount = 0;
  QString timeRangeStartIso;
  QString timeRangeEndIso;
  bool gridCompatible = true;
  QString commonRadiometricState;       ///< "" when unknown
  bool uniformScaleOffset = true;
  double uniformScale = 1.0;            ///< meaningful when uniformScaleOffset && scaleDefined
  double uniformOffset = 0.0;
  bool scaleOffsetDeclared = false;     ///< false = no scene declares scale/offset (raw values)
  QVector<SceneRadiometry> radiometry;  ///< per scene (chronological order)
  ModalityProfile modality;             ///< Platform 3.0 multimodal facts

  bool ok() const;
  PreflightIssue firstBlocking() const;
  Json::Value toJson() const;
};

/// Analysis band used for the radiometric checks: @a analysisBandOverride when
/// > 0, else the resolved band for @a analysisRoleId, else band 1.
///
/// Opens every scene once (read-only metadata pass), verifies all sections,
/// and returns per-scene radiometry for the streaming reader. Never mutates
/// the collection.
TemporalPreflightReport runPreflight( const TemporalCollection &collection,
                                      const PreflightOptions &options,
                                      const QString &analysisRoleId = QString(),
                                      int analysisBandOverride = 0 );

} // namespace sicnu::temporal
