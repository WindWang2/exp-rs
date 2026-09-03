// src/data/temporal_workspace_types.h
// TemporalCollection as a first-class workspace record.
//
// The DataManager owns the collection's identity (id + revision) and its
// canonical descriptor document. The descriptor is the OPAQUE temporal schema
// owned by the temporal layer (sicnu::temporal::TemporalCollection); that
// layer binds each scene to the registered Data Assets it references and
// converts between the typed collection and the stored document. The data
// layer stores it verbatim — storing JSON text keeps sicnu_data free of any
// dependency on the temporal/processing layer.
//
// The record is deliberately NOT a new AssetKind: a collection is a catalog
// entity, not a renderable dataset, and an AssetKind would ripple through
// every exhaustive kind switch (workspace snapshot, structures-compat,
// serializer) without adding capability. It mirrors the existing Data
// Collection machinery instead: create/restore/list/update/remove + signals +
// a dedicated project-serializer block.
#pragma once

#include "collection_types.h"
#include "data_result.h"

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QtTypes>

namespace sicnu::data
{

/// A registered TemporalCollection in the catalog.
struct TemporalCollectionRecord
{
  CollectionId id;
  QString displayName;
  /// Canonical descriptor JSON (schema "exp_rs_temporal_collection/1"),
  /// stored verbatim. Scene identity: per-scene assetId + assetRevision when
  /// the scene resolves to a registered Data Asset, with the path kept as
  /// fallback/diagnostics.
  QString descriptor;
  /// Monotonic version of THIS record (bumped by updateTemporalCollection).
  /// Scene-level data identity lives in the descriptor's per-scene asset
  /// revisions; consumers that must detect a scene-content change resolve the
  /// scene's CURRENT asset revision from the DataManager at use time (never a
  /// stale stored snapshot).
  quint64 revision = 1;
  QDateTime createdAtUtc;
  QDateTime updatedAtUtc;
};

struct TemporalCollectionCreateRequest
{
  QString displayName;
  QString descriptor;
};

struct TemporalCollectionCreateResult
{
  CollectionId collectionId;
  /// True when an identical (name + descriptor) record already existed and
  /// was returned instead of creating a duplicate.
  bool reusedExisting = false;
  QVector<Diagnostic> diagnostics;
};

} // namespace sicnu::data
