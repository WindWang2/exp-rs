#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
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

/// Input-side counterpart of TaskCenter's findOutputPathInParams (#698):
/// collects parameter values that are existing FILE paths, regardless of the
/// parameter key — restricting the scan to keys mentioning "input" silently
/// dropped real lineage edges for parameters like dNBR's "postfire" or
/// fusion's "pan"/"ms" (#718). Placeholder references ("$step.output"),
/// directories and non-path strings are ignored — only paths that exist on
/// disk at commit time can be resolved into lineage records. Paths in
/// @a excludePaths (callers pass the task's own output path so a re-run to an
/// existing output never becomes its own input) are skipped. String,
/// string-list and variant-list values are all considered. ONE implementation
/// shared by the tool-call dispatcher and the CLI pipeline runner.
QStringList findInputPathsInParams( const QVariantMap &params,
                                    const QStringList &excludePaths = QStringList() );

/// Resolved input lineage for one run: paths that map to a registered asset
/// become DerivationInput records (asset id + the revision that was present);
/// anything else is kept in unresolvedPaths so provenance reports it instead
/// of dropping it silently.
struct InputLineage
{
  QVector<DerivationInput> inputs;
  QStringList unresolvedPaths;
};

InputLineage resolveInputLineage( class DataManager *dataManager, const QStringList &paths );

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
  /// Input paths the run parameters referenced that could NOT be resolved to
  /// registered Data Assets when the record was built (not yet registered,
  /// mis-spelled, or produced mid-pipeline). Diagnostics-only (#698): the raw
  /// reference is preserved here instead of being dropped silently — the
  /// lineage graph records what resolved, and this records what did not.
  QStringList unresolvedInputPaths;
  AssetId outputAssetId;
  QString taskReference;
  QString softwareVersion;
  QDateTime completedAtUtc;
  /// Non-secret authentication configuration reference for the execution
  /// context (e.g. the auth config used to reach remote inputs). Never a
  /// password, token, or other credential material.
  QString authConfigId;
  QString workflowId;
  QString workflowRunId;
  QString stepId;

  QJsonObject toJson() const;

  /// Parses a record from JSON. Returns a `derivation.invalid` diagnostic when
  /// an Asset ID string is present but not a valid Asset ID.
  static Result<DerivationRecord> fromJson( const QJsonObject &json );

  friend bool operator==( const DerivationRecord &, const DerivationRecord & ) = default;
};

/// Builds a DerivationRecord for a completed algorithm task: algorithm id,
/// parameter snapshot, task reference, and completion timestamp. The output
/// Asset ID is stamped by DataManager::attachDerivationRecord. Shared by the
/// CLI pipeline runner and TaskCenter-backed committers so every produced
/// asset records the same provenance shape. When the caller resolved the run's
/// input paths against the catalog, the resolved lineage rides in `inputs`
/// and anything unresolvable in `unresolvedInputPaths` (#698).
inline DerivationRecord makeTaskDerivation( const QString &algorithmId,
                                            const QJsonObject &parameters,
                                            const QString &taskReference,
                                            QVector<DerivationInput> inputs = {},
                                            const QStringList &unresolvedInputPaths = {} )
{
  DerivationRecord record;
  record.algorithmId = algorithmId;
  record.parameters = parameters;
  record.inputs = std::move( inputs );
  record.unresolvedInputPaths = unresolvedInputPaths;
  record.taskReference = taskReference;
  record.completedAtUtc = QDateTime::currentDateTimeUtc();
  return record;
}

/// Builds a DerivationRecord for a workflow run step execution. Like
/// makeTaskDerivation, the caller may stamp the resolved input lineage (#698).
inline DerivationRecord makeWorkflowDerivation( const QString &algorithmId,
                                                const QJsonObject &parameters,
                                                const QString &workflowId,
                                                const QString &workflowRunId,
                                                const QString &stepId,
                                                const QString &taskReference = QString(),
                                                QVector<DerivationInput> inputs = {},
                                                const QStringList &unresolvedInputPaths = {} )
{
  DerivationRecord record;
  record.algorithmId = algorithmId;
  record.parameters = parameters;
  record.workflowId = workflowId;
  record.workflowRunId = workflowRunId;
  record.stepId = stepId;
  record.taskReference = taskReference;
  record.inputs = std::move( inputs );
  record.unresolvedInputPaths = unresolvedInputPaths;
  record.completedAtUtc = QDateTime::currentDateTimeUtc();
  return record;
}

} // namespace sicnu::data
