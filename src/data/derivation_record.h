#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "asset_types.h"
#include "data_result.h"

namespace sicnu::data
{

/// One algorithm input as recorded for provenance: the Data Asset identity and
/// the exact revision that was read, plus band references and the value domain
/// where applicable. It is a plain value with no live handles.
struct DerivationInput
{
  AssetId assetId;
  AssetRevision revision;
  QStringList bandReferences;
  QString valueDomain;

  friend bool operator==( const DerivationInput &, const DerivationInput & ) = default;
};

/// Structured, serializable record describing how a derived Data Asset was
/// produced: algorithm ID and version, a snapshot of the parameters, the input
/// Asset IDs with their revisions, the output Asset ID, and execution
/// information (task reference, software version, completion timestamp).
///
/// It is a plain value with no live handles and no credentials — sensitive
/// authentication material is excluded by construction; only the non-secret
/// `authConfigId` reference may appear, consistent with the Data Asset
/// descriptor rules.
struct DerivationRecord
{
  QString algorithmId;
  QString algorithmVersion;
  /// Snapshot of the algorithm parameters. JSON-native by type, so it
  /// serializes losslessly by construction. It carries algorithm inputs only
  /// (band indices, thresholds, output options) — never credential material;
  /// remote access is represented solely by `authConfigId` below.
  QJsonObject parameters;
  QVector<DerivationInput> inputs;
  AssetId outputAssetId;
  QString taskReference;
  QString softwareVersion;
  QDateTime completedAtUtc;
  /// Non-secret authentication configuration reference for the execution
  /// context (e.g. the auth config used to reach remote inputs). Never a
  /// password, token, or other credential material.
  QString authConfigId;

  QJsonObject toJson() const;

  /// Parses a record from JSON. Returns a `derivation.invalid` diagnostic when
  /// an Asset ID string is present but not a valid Asset ID.
  static Result<DerivationRecord> fromJson( const QJsonObject &json );

  friend bool operator==( const DerivationRecord &, const DerivationRecord & ) = default;
};

} // namespace sicnu::data
