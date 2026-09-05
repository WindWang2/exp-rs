// src/processing/algorithms/temporal/spatiotemporal_contracts.h
// SpatioTemporal observation contracts — the typed, modality-aware view over a
// TemporalCollection (Platform 3.0, goal §5).
//
// TemporalCollection stays the persistence + exchange spine; this layer adds
// the normalized semantic vocabulary (modality, sensor, polarizations, band
// roles, quality) so SAR / DEM / Optical scenes can share one identity:
//
//   TemporalCollection ──contractsOf()──▶ QVector<ObservationContract>
//
// The contracts are derived, never persisted: every field already round-trips
// through the v1 descriptor (serialized only when claimed), so legacy project
// files stay byte-stable. Nothing here branches the temporal fold math —
// preflight and agent surfaces consume the typed view; kernels stay
// modality-neutral.
#pragma once

#include "temporal_collection.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <map>

namespace sicnu::temporal
{

/// Observation modality. The string vocabulary matches the descriptor
/// validator's (`algorithm_descriptor_validator.cpp`) so operator contracts,
/// agent facet filters, and scene descriptors speak one language.
enum class Modality : quint8
{
  Unknown,
  Optical,
  Sar,
  Dem,
  Auxiliary,
  ModelDerived,
};

/// Parses "optical"|"sar"|"dem"|"auxiliary"|"model"|"multimodal"|"" (case
/// insensitive). Unknown on anything else; "multimodal" maps to Unknown here —
/// multimodality is a property of a collection, not one observation.
Modality modalityFromString( const QString &token );
QString modalityToString( Modality modality );

/// Normalizes a polarization token to the canonical upper-case form
/// ("vv" → "VV", "vh" → "VH", "co-pol" keeps its vocabulary shape).
/// Returns a normalized copy; unknown tokens are upper-cased best effort.
QString normalizePolarization( const QString &token );
QStringList normalizePolarizations( const QStringList &tokens );

/// Infers the observation modality from the clues a scene already carries
/// (platform / sensor naming, radiometric state, band roles). Returns
/// Modality::Unknown when nothing is conclusive — inference never overrides an
/// explicitly claimed modality.
Modality inferModalityFromClues( const QString &platform,
                                 const QString &sensor,
                                 const QString &radiometricState,
                                 const QStringList &bandRoles );

/// The normalized per-scene semantic view. Derived from a TemporalSceneRef in
/// O(1); no pixel or file access.
struct ObservationContract
{
  QString observationId;        ///< asset id when bound, else a stable path digest
  QString path;                 ///< pixel owner (the scene ref's path)
  QString assetId;
  QString assetRevision;
  QString collectionId;
  QString collectionRevision;
  Modality modality = Modality::Unknown;
  QString sensor;
  QString platform;
  QString processingLevel;
  AcquisitionTime time;
  QStringList bandRoles;        ///< declared roles (overrides keys + declared list, deduped, input order)
  QStringList polarizations;    ///< normalized (upper case)
  std::map<QString, int> bandOverrides;  ///< role id → 1-based band (as declared)
  QString radiometricState;
  double resolutionMeters = 0.0;
  double cloudCoverPercent = -1.0;
  int qualityBand = 0;
  int maskBand = 0;
  int originalIndex = 0;

  static ObservationContract fromSceneRef( const TemporalSceneRef &scene,
                                           const QString &collectionId = QString(),
                                           const QString &collectionRevision = QString() );
};

/// Derives the full contract view of a collection (chronological order).
QVector<ObservationContract> contractsOf( const TemporalCollection &collection );

/// The distinct modalities claimed by a set of contracts (order: Optical, Sar,
/// Dem, Auxiliary, ModelDerived; Unknown reported as "unknown" only when it is
/// the ONLY verdict). Never empty for a non-empty collection.
QStringList distinctModalities( const QVector<ObservationContract> &contracts );

} // namespace sicnu::temporal
