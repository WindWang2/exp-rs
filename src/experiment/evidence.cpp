// evidence.cpp — Automatic Scientific Evidence projection (goal M4).
//
// Projection only: every block below reads recorded fields and reports
// presence/absence. The failure paths are the point — a run whose model
// digest, artifact digests or metrics are missing produces an INCOMPLETE
// verdict naming the gaps, because "looks complete" is how unreproducible
// results survive.
#include "evidence.h"

#include <QJsonArray>

namespace sicnu::experiment
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

namespace
{

Diagnostic evidenceError( const QString &message )
{
    return Diagnostic{ QStringLiteral( "experiment.evidence_invalid" ), message,
                       DiagnosticSeverity::Error };
}

QJsonObject identityBlock( const ExperimentRun &run )
{
    QJsonObject identity;
    identity.insert( QStringLiteral( "dataset_version" ), run.datasetVersionId() );
    identity.insert( QStringLiteral( "dataset_fingerprint" ), run.datasetFingerprint() );
    identity.insert( QStringLiteral( "split_manifest" ), run.splitManifestId() );
    identity.insert( QStringLiteral( "split_fingerprint" ), run.splitFingerprint() );
    identity.insert( QStringLiteral( "model" ), run.modelId() );
    identity.insert( QStringLiteral( "model_digest" ), run.modelDigest() );
    identity.insert( QStringLiteral( "seed" ), QString::number( run.seed() ) );
    identity.insert( QStringLiteral( "software_revision" ), run.softwareRevision() );
    identity.insert( QStringLiteral( "algorithm" ), run.algorithmId() );
    identity.insert( QStringLiteral( "algorithm_version" ), run.algorithmVersion() );
    return identity;
}

/// Step evidence lives in the metrics document under "workflow" (bridge
/// contract). Absent key = no step evidence, which is honest for runs
/// recorded without workflow scope.
std::optional<QJsonObject> workflowEvidence( const ExperimentRun &run )
{
    const QJsonObject workflow = run.metrics().value( QStringLiteral( "workflow" ) ).toObject();
    if ( workflow.isEmpty() )
        return std::nullopt;
    return workflow;
}

} // namespace

bool EvidenceCompleteness::complete() const
{
    for ( const Dimension &dimension : dimensions )
    {
        if ( !dimension.present )
            return false;
    }
    return true;
}

QStringList EvidenceCompleteness::missing() const
{
    QStringList names;
    for ( const Dimension &dimension : dimensions )
    {
        if ( !dimension.present )
            names.append( dimension.name );
    }
    return names;
}

QJsonObject EvidenceCompleteness::toJson() const
{
    QJsonArray array;
    for ( const Dimension &dimension : dimensions )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "name" ), dimension.name );
        item.insert( QStringLiteral( "present" ), dimension.present );
        if ( !dimension.present )
            item.insert( QStringLiteral( "detail" ), dimension.detail );
        array.append( item );
    }
    QJsonObject json;
    json.insert( QStringLiteral( "dimensions" ), array );
    json.insert( QStringLiteral( "complete" ), complete() );
    return json;
}

Result<QJsonObject> EvidenceProjector::summarize( const Input &input )
{
    const ExperimentRun &run = input.run;
    if ( run.runId().isEmpty() )
        return Result<QJsonObject>::failure(
            evidenceError( QStringLiteral( "run record requires a run id" ) ) );

    QJsonObject summary;
    summary.insert( QStringLiteral( "schema_version" ), kEvidenceSchemaVersion );
    summary.insert( QStringLiteral( "run_id" ), run.runId() );
    summary.insert( QStringLiteral( "experiment_id" ), run.experimentId() );
    summary.insert( QStringLiteral( "status" ),
                    sicnu::dataset::runStatusToString( run.status() ) );
    summary.insert( QStringLiteral( "identity" ), identityBlock( run ) );
    summary.insert( QStringLiteral( "environment" ), run.environment().redacted().toJson() );

    QJsonArray artifacts;
    bool digested = !run.artifacts().isEmpty();
    for ( const ExperimentRun::Artifact &artifact : run.artifacts() )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "path" ), artifact.path );
        item.insert( QStringLiteral( "role" ), artifact.role );
        item.insert( QStringLiteral( "digest" ), artifact.digest );
        item.insert( QStringLiteral( "size_bytes" ), artifact.sizeBytes );
        artifacts.append( item );
        if ( artifact.digest.isEmpty() )
            digested = false;
    }
    summary.insert( QStringLiteral( "artifacts" ), artifacts );

    QJsonObject completenessJson;
    EvidenceCompleteness completeness;
    auto dimension = []( const QString &name, bool present, const QString &detail ) {
        EvidenceCompleteness::Dimension d;
        d.name = name;
        d.present = present;
        d.detail = detail;
        return d;
    };

    // Identity: a citable run pins WHAT it was (dataset/split/model/seed).
    {
        const bool hasIdentity =
            !run.datasetVersionId().isEmpty() || !run.modelDigest().isEmpty();
        completeness.dimensions.append( dimension(
            QStringLiteral( "identity" ), hasIdentity,
            QStringLiteral( "no dataset version or model digest pinned" ) ) );
    }
    // Environment: where it ran (always captured by the recorder, but an
    // assembled-by-hand run may lack it).
    {
        const bool hasEnvironment = !run.environment().toJson().isEmpty();
        completeness.dimensions.append( dimension(
            QStringLiteral( "environment" ), hasEnvironment,
            QStringLiteral( "no environment snapshot recorded" ) ) );
    }
    // Artifacts: outputs exist AND carry digests (undigested artifacts do
    // not prove what came out).
    completeness.dimensions.append(
        dimension( QStringLiteral( "artifacts" ), digested,
                   run.artifacts().isEmpty()
                       ? QStringLiteral( "no artifacts recorded" )
                       : QStringLiteral( "some artifacts lack content digests" ) ) );
    // Metrics: protocol + document + hash.
    if ( input.metricRecord.has_value() )
    {
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "hash" ), input.metricRecord->metricsHash );
        metrics.insert( QStringLiteral( "protocol" ), input.metricRecord->protocol.toJson() );
        metrics.insert( QStringLiteral( "schema_version" ), kMetricsSchemaVersion );
        metrics.insert( QStringLiteral( "document" ), input.metricRecord->metrics );
        summary.insert( QStringLiteral( "metrics" ), metrics );
        completeness.dimensions.append(
            dimension( QStringLiteral( "metrics" ),
                       !input.metricRecord->metricsHash.isEmpty(),
                       QStringLiteral( "metric record carries no content hash" ) ) );
    }
    else
    {
        summary.insert( QStringLiteral( "metrics" ), QJsonObject{} );
        completeness.dimensions.append( dimension(
            QStringLiteral( "metrics" ), false,
            QStringLiteral( "no metric record persisted for this run" ) ) );
    }
    // Steps: workflow evidence, when the execution plane reported it.
    if ( const auto stepsEvidence = workflowEvidence( run ) )
    {
        const QJsonArray steps = stepsEvidence->value( QStringLiteral( "steps" ) ).toArray();
        qint64 completed = 0;
        qint64 failed = 0;
        for ( const QJsonValue &value : steps )
        {
            const QString status =
                value.toObject().value( QStringLiteral( "status" ) ).toString();
            if ( status == QStringLiteral( "Completed" ) )
                ++completed;
            else if ( status == QStringLiteral( "Failed" ) )
                ++failed;
        }
        QJsonObject stepsJson;
        stepsJson.insert( QStringLiteral( "count" ), steps.size() );
        stepsJson.insert( QStringLiteral( "completed" ), completed );
        stepsJson.insert( QStringLiteral( "failed" ), failed );
        summary.insert( QStringLiteral( "steps" ), stepsJson );
        completeness.dimensions.append( dimension(
            QStringLiteral( "steps" ), !steps.isEmpty(),
            QStringLiteral( "workflow evidence carries no steps" ) ) );
    }
    else
    {
        summary.insert( QStringLiteral( "steps" ), QJsonObject{} );
        completeness.dimensions.append( dimension(
            QStringLiteral( "steps" ), false,
            QStringLiteral( "run was recorded without workflow step evidence" ) ) );
    }
    // Timing.
    {
        const bool hasTiming = run.startedAtUtc().isValid() && run.finishedAtUtc().isValid();
        completeness.dimensions.append( dimension(
            QStringLiteral( "timing" ), hasTiming,
            QStringLiteral( "start/finish timestamps are incomplete" ) ) );
    }

    completenessJson = completeness.toJson();
    summary.insert( QStringLiteral( "completeness" ), completenessJson );
    return Result<QJsonObject>::success( summary );
}

} // namespace sicnu::experiment
