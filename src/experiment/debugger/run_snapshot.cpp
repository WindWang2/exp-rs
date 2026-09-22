// run_snapshot.cpp — normalized run snapshot value type + canonical digest
// (RS14-06, ADR 0174). Slice A GREEN implementation.

#include "run_snapshot.h"

#include "../../data/execution_fingerprint.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>

namespace sicnu::experiment::debugger
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

namespace
{

Diagnostic malformedSnapshot( const QString &detail )
{
    return { QLatin1String( kCodeMalformedSnapshot ),
             QStringLiteral( "snapshot document rejected: %1" ).arg( detail ),
             DiagnosticSeverity::Error };
}

QJsonObject stepToJson( const StepSnapshot &step )
{
    QJsonObject json;
    json.insert( QLatin1String( "step_id" ), step.stepId );
    json.insert( QLatin1String( "operator_id" ), step.operatorId );
    if ( !step.parameters.isEmpty() )
        json.insert( QLatin1String( "parameters" ), step.parameters );
    json.insert( QLatin1String( "params_hash" ), step.paramsHash );
    json.insert( QLatin1String( "lineage_signature" ), step.lineageSignature );
    json.insert( QLatin1String( "status" ), step.status );
    if ( step.cacheHitKnown )
    {
        json.insert( QLatin1String( "cache_hit" ), step.cacheHit );
        json.insert( QLatin1String( "cache_hit_known" ), true );
    }
    if ( !step.dependencies.isEmpty() )
    {
        QJsonArray deps;
        for ( const QString &dep : step.dependencies )
            deps.append( dep );
        json.insert( QLatin1String( "dependencies" ), deps );
    }
    json.insert( QLatin1String( "output_digest" ), step.outputDigest );
    json.insert( QLatin1String( "digest_mode" ), step.digestMode );
    if ( step.outputSizeBytes >= 0 )
        json.insert( QLatin1String( "output_size_bytes" ), static_cast<double>( step.outputSizeBytes ) );
    if ( !step.errorMessage.isEmpty() )
        json.insert( QLatin1String( "error_message" ), step.errorMessage );
    return json;
}

} // namespace

QString stepEvidenceModeName( StepEvidenceMode mode )
{
    switch ( mode )
    {
        case StepEvidenceMode::Absent:
            return QStringLiteral( "absent" );
        case StepEvidenceMode::CheckpointSteps:
            return QStringLiteral( "checkpoint_steps" );
        case StepEvidenceMode::ProvenanceGraph:
            return QStringLiteral( "provenance_graph" );
        case StepEvidenceMode::StepsEvidence:
            return QStringLiteral( "steps_evidence" );
    }
    return QStringLiteral( "absent" );
}

Result<StepEvidenceMode> stepEvidenceModeFromName( const QString &name )
{
    if ( name == QLatin1String( "absent" ) )
        return Result<StepEvidenceMode>::success( StepEvidenceMode::Absent );
    if ( name == QLatin1String( "checkpoint_steps" ) )
        return Result<StepEvidenceMode>::success( StepEvidenceMode::CheckpointSteps );
    if ( name == QLatin1String( "provenance_graph" ) )
        return Result<StepEvidenceMode>::success( StepEvidenceMode::ProvenanceGraph );
    if ( name == QLatin1String( "steps_evidence" ) )
        return Result<StepEvidenceMode>::success( StepEvidenceMode::StepsEvidence );
    return Result<StepEvidenceMode>::failure(
        malformedSnapshot( QStringLiteral( "unknown step_evidence mode '%1'" ).arg( name ) ) );
}

DigestRecord classifyRecordedDigest( const QString &recorded )
{
    DigestRecord record;
    record.digest = recorded;
    if ( recorded.isEmpty() )
    {
        record.mode = QLatin1String( kDigestModeUnknown );
        return record;
    }
    // Shape check only — this module never COMPUTES digests, so a local
    // 64-hex probe introduces no second digest authority.
    static const QRegularExpression hex64( QStringLiteral( "^[0-9a-fA-F]{64}$" ) );
    if ( recorded.startsWith( QLatin1String( "sha256fl:" ) ) )
        record.mode = QLatin1String( kDigestModeSha256Fl );
    else if ( recorded.startsWith( QLatin1String( "sha256full:" ) ) )
        record.mode = QLatin1String( kDigestModeSha256Full );
    else if ( hex64.match( recorded ).hasMatch() )
        record.mode = QLatin1String( kDigestModeSha256Hex );
    else
        record.mode = QLatin1String( kDigestModeUnknown );
    return record;
}

QJsonObject StepSnapshot::toJson() const
{
    return stepToJson( *this );
}

Result<StepSnapshot> StepSnapshot::fromJson( const QJsonObject &json )
{
    StepSnapshot step;
    step.stepId = json.value( QLatin1String( "step_id" ) ).toString();
    step.operatorId = json.value( QLatin1String( "operator_id" ) ).toString();
    step.parameters = json.value( QLatin1String( "parameters" ) ).toObject();
    step.paramsHash = json.value( QLatin1String( "params_hash" ) ).toString();
    step.lineageSignature = json.value( QLatin1String( "lineage_signature" ) ).toString();
    step.status = json.value( QLatin1String( "status" ) ).toString();
    step.cacheHitKnown = json.value( QLatin1String( "cache_hit_known" ) ).toBool( false );
    step.cacheHit = json.value( QLatin1String( "cache_hit" ) ).toBool( false );
    const QJsonArray deps = json.value( QLatin1String( "dependencies" ) ).toArray();
    for ( const QJsonValue &dep : deps )
        step.dependencies.append( dep.toString() );
    step.outputDigest = json.value( QLatin1String( "output_digest" ) ).toString();
    step.digestMode = json.value( QLatin1String( "digest_mode" ) ).toString();
    step.outputSizeBytes = static_cast<qint64>(
        json.value( QLatin1String( "output_size_bytes" ) ).toDouble( -1 ) );
    step.errorMessage = json.value( QLatin1String( "error_message" ) ).toString();

    if ( step.stepId.isEmpty() )
        return Result<StepSnapshot>::failure( malformedSnapshot( QStringLiteral( "step_id is empty" ) ) );
    if ( step.dependencies.removeDuplicates() > 0 )
        return Result<StepSnapshot>::failure( malformedSnapshot( QStringLiteral( "duplicate dependency" ) ) );
    return Result<StepSnapshot>::success( step );
}

QJsonObject ArtifactSnapshot::toJson() const
{
    QJsonObject json;
    json.insert( QLatin1String( "artifact_id" ), artifactId );
    json.insert( QLatin1String( "digest" ), digest );
    json.insert( QLatin1String( "digest_mode" ), digestMode );
    if ( sizeBytes >= 0 )
        json.insert( QLatin1String( "size_bytes" ), static_cast<double>( sizeBytes ) );
    json.insert( QLatin1String( "root_input" ), rootInput );
    if ( !producerStepId.isEmpty() )
        json.insert( QLatin1String( "producer_step_id" ), producerStepId );
    return json;
}

Result<ArtifactSnapshot> ArtifactSnapshot::fromJson( const QJsonObject &json )
{
    ArtifactSnapshot artifact;
    artifact.artifactId = json.value( QLatin1String( "artifact_id" ) ).toString();
    artifact.digest = json.value( QLatin1String( "digest" ) ).toString();
    artifact.digestMode = json.value( QLatin1String( "digest_mode" ) ).toString();
    artifact.sizeBytes = static_cast<qint64>( json.value( QLatin1String( "size_bytes" ) ).toDouble( -1 ) );
    artifact.rootInput = json.value( QLatin1String( "root_input" ) ).toBool( false );
    artifact.producerStepId = json.value( QLatin1String( "producer_step_id" ) ).toString();
    if ( artifact.artifactId.isEmpty() )
        return Result<ArtifactSnapshot>::failure(
            malformedSnapshot( QStringLiteral( "artifact_id is empty" ) ) );
    return Result<ArtifactSnapshot>::success( artifact );
}

QJsonObject RunPinsSnapshot::toJson() const
{
    QJsonObject json;
    json.insert( QLatin1String( "algorithm_id" ), algorithmId );
    json.insert( QLatin1String( "algorithm_version" ), algorithmVersion );
    json.insert( QLatin1String( "dataset_version_id" ), datasetVersionId );
    json.insert( QLatin1String( "dataset_fingerprint" ), datasetFingerprint );
    json.insert( QLatin1String( "split_manifest_id" ), splitManifestId );
    json.insert( QLatin1String( "split_fingerprint" ), splitFingerprint );
    json.insert( QLatin1String( "model_id" ), modelId );
    json.insert( QLatin1String( "model_digest" ), modelDigest );
    json.insert( QLatin1String( "software_revision" ), softwareRevision );
    json.insert( QLatin1String( "config_hash" ), configHash );
    json.insert( QLatin1String( "result_fingerprint" ), resultFingerprint );
    json.insert( QLatin1String( "run_status" ), runStatus );
    if ( seedKnown )
    {
        json.insert( QLatin1String( "seed" ), static_cast<double>( seed ) );
        json.insert( QLatin1String( "seed_known" ), true );
    }
    return json;
}

Result<RunPinsSnapshot> RunPinsSnapshot::fromJson( const QJsonObject &json )
{
    RunPinsSnapshot pins;
    pins.algorithmId = json.value( QLatin1String( "algorithm_id" ) ).toString();
    pins.algorithmVersion = json.value( QLatin1String( "algorithm_version" ) ).toString();
    pins.datasetVersionId = json.value( QLatin1String( "dataset_version_id" ) ).toString();
    pins.datasetFingerprint = json.value( QLatin1String( "dataset_fingerprint" ) ).toString();
    pins.splitManifestId = json.value( QLatin1String( "split_manifest_id" ) ).toString();
    pins.splitFingerprint = json.value( QLatin1String( "split_fingerprint" ) ).toString();
    pins.modelId = json.value( QLatin1String( "model_id" ) ).toString();
    pins.modelDigest = json.value( QLatin1String( "model_digest" ) ).toString();
    pins.softwareRevision = json.value( QLatin1String( "software_revision" ) ).toString();
    pins.configHash = json.value( QLatin1String( "config_hash" ) ).toString();
    pins.resultFingerprint = json.value( QLatin1String( "result_fingerprint" ) ).toString();
    pins.runStatus = json.value( QLatin1String( "run_status" ) ).toString();
    pins.seedKnown = json.value( QLatin1String( "seed_known" ) ).toBool( false );
    pins.seed = static_cast<quint64>( json.value( QLatin1String( "seed" ) ).toDouble( 0 ) );
    return Result<RunPinsSnapshot>::success( pins );
}

const StepSnapshot *RunSnapshot::findStep( const QString &stepId ) const
{
    for ( const StepSnapshot &step : m_steps )
        if ( step.stepId == stepId )
            return &step;
    return nullptr;
}

QJsonObject RunSnapshot::toJson() const
{
    QJsonObject json;
    json.insert( QLatin1String( "kind" ), QLatin1String( kSnapshotKind ) );
    json.insert( QLatin1String( "schema_version" ), kDebuggerSchemaVersion );
    json.insert( QLatin1String( "run_id" ), m_runId );
    json.insert( QLatin1String( "step_evidence" ), stepEvidenceModeName( m_stepEvidence ) );
    json.insert( QLatin1String( "plan_signature" ), m_planSignature );

    QJsonArray steps;
    for ( const StepSnapshot &step : m_steps )
        steps.append( step.toJson() );
    json.insert( QLatin1String( "steps" ), steps );

    QJsonArray artifacts;
    for ( const ArtifactSnapshot &artifact : m_artifacts )
        artifacts.append( artifact.toJson() );
    json.insert( QLatin1String( "artifacts" ), artifacts );

    json.insert( QLatin1String( "pins" ), m_pins.toJson() );
    json.insert( QLatin1String( "metrics" ), m_metrics );
    return json;
}

Result<RunSnapshot> RunSnapshot::fromJson( const QJsonObject &json )
{
    if ( json.value( QLatin1String( "kind" ) ).toString() != QLatin1String( kSnapshotKind ) )
        return Result<RunSnapshot>::failure(
            malformedSnapshot( QStringLiteral( "unsupported kind" ) ) );
    if ( json.value( QLatin1String( "schema_version" ) ).toInt() != kDebuggerSchemaVersion )
        return Result<RunSnapshot>::failure(
            malformedSnapshot( QStringLiteral( "unsupported schema version" ) ) );

    RunSnapshot snapshot;
    snapshot.m_runId = json.value( QLatin1String( "run_id" ) ).toString();
    if ( snapshot.m_runId.isEmpty() )
        return Result<RunSnapshot>::failure( malformedSnapshot( QStringLiteral( "run_id is empty" ) ) );

    const auto mode = stepEvidenceModeFromName(
        json.value( QLatin1String( "step_evidence" ) ).toString() );
    if ( !mode )
        return Result<RunSnapshot>::failure( mode.diagnostics() );
    snapshot.m_stepEvidence = mode.value();

    snapshot.m_planSignature = json.value( QLatin1String( "plan_signature" ) ).toString();

    const QJsonArray steps = json.value( QLatin1String( "steps" ) ).toArray();
    if ( steps.size() > kMaxSnapshotSteps )
        return Result<RunSnapshot>::failure(
            malformedSnapshot( QStringLiteral( "step count exceeds the snapshot cap" ) ) );
    for ( const QJsonValue &value : steps )
    {
        auto step = StepSnapshot::fromJson( value.toObject() );
        if ( !step )
            return Result<RunSnapshot>::failure( step.diagnostics() );
        if ( snapshot.findStep( step->stepId ) )
            return Result<RunSnapshot>::failure(
                malformedSnapshot( QStringLiteral( "duplicate step id '%1'" ).arg( step->stepId ) ) );
        snapshot.m_steps.append( step.take() );
    }

    const QJsonArray artifacts = json.value( QLatin1String( "artifacts" ) ).toArray();
    if ( artifacts.size() > kMaxSnapshotArtifacts )
        return Result<RunSnapshot>::failure(
            malformedSnapshot( QStringLiteral( "artifact count exceeds the snapshot cap" ) ) );
    for ( const QJsonValue &value : artifacts )
    {
        auto artifact = ArtifactSnapshot::fromJson( value.toObject() );
        if ( !artifact )
            return Result<RunSnapshot>::failure( artifact.diagnostics() );
        snapshot.m_artifacts.append( artifact.take() );
    }

    auto pins = RunPinsSnapshot::fromJson( json.value( QLatin1String( "pins" ) ).toObject() );
    if ( !pins )
        return Result<RunSnapshot>::failure( pins.diagnostics() );
    snapshot.m_pins = pins.take();

    snapshot.m_metrics = json.value( QLatin1String( "metrics" ) ).toObject();
    return Result<RunSnapshot>::success( snapshot );
}

QJsonObject RunSnapshot::identityDocument() const
{
    // Identity content: everything that defines WHAT ran and what it
    // produced — pins, plan signature, per-step process identity and output
    // identity, root input identity. Deliberately excluded (volatile or
    // presentational): run status label, step status strings, timings,
    // sizes, error text, artifact paths, metrics content. Two snapshots of
    // the same execution carry the same identity; any identity-bearing
    // difference changes it.
    QJsonObject pinsForDigest = m_pins.toJson();
    pinsForDigest.remove( QLatin1String( "run_status" ) );

    QJsonObject identity;
    identity.insert( QLatin1String( "pins" ), pinsForDigest );
    identity.insert( QLatin1String( "plan_signature" ), m_planSignature );

    QJsonArray steps;
    for ( const StepSnapshot &step : m_steps )
    {
        QJsonObject entry;
        entry.insert( QLatin1String( "step_id" ), step.stepId );
        entry.insert( QLatin1String( "operator_id" ), step.operatorId );
        entry.insert( QLatin1String( "params_hash" ), step.paramsHash );
        entry.insert( QLatin1String( "lineage_signature" ), step.lineageSignature );
        QStringList deps = step.dependencies;
        std::sort( deps.begin(), deps.end() );
        QJsonArray depsJson;
        for ( const QString &dep : deps )
            depsJson.append( dep );
        entry.insert( QLatin1String( "dependencies" ), depsJson );
        entry.insert( QLatin1String( "output_digest" ), step.outputDigest );
        entry.insert( QLatin1String( "output_digest_mode" ), step.digestMode );
        steps.append( entry );
    }
    identity.insert( QLatin1String( "steps" ), steps );

    // Root inputs sorted by artifact id: external input state is identity.
    QList<ArtifactSnapshot> roots;
    for ( const ArtifactSnapshot &artifact : m_artifacts )
        if ( artifact.rootInput )
            roots.append( artifact );
    std::sort( roots.begin(), roots.end(),
               []( const ArtifactSnapshot &a, const ArtifactSnapshot &b ) {
                   return a.artifactId < b.artifactId;
               } );
    QJsonArray rootsJson;
    for ( const ArtifactSnapshot &root : roots )
    {
        QJsonObject entry;
        entry.insert( QLatin1String( "artifact_id" ), root.artifactId );
        entry.insert( QLatin1String( "digest" ), root.digest );
        entry.insert( QLatin1String( "digest_mode" ), root.digestMode );
        rootsJson.append( entry );
    }
    identity.insert( QLatin1String( "root_inputs" ), rootsJson );

    return identity;
}

QString RunSnapshot::snapshotDigest() const
{
    QJsonObject identity = identityDocument();
    identity.insert( QLatin1String( "run_id" ), m_runId );
    const QByteArray canonical = sicnu::data::canonicalizeJsonRfc8785( identity );
    return QString::fromLatin1(
        QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 ).toHex() );
}

} // namespace sicnu::experiment::debugger
