// experiment_types.cpp — run identity, environment capture, comparison.
#include "experiment_types.h"

#include "../data/execution_fingerprint.h" // canonical JSON + SHA-256 reuse

#include <algorithm>
#include <cmath>

#include <QCryptographicHash>
#include <QDate>
#include <QHashIterator>
#include <QJsonArray>
#include <QLocale>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTimeZone>

namespace sicnu::experiment
{

namespace
{

QString hashCanonical( const QJsonObject &json )
{
    return QString::fromUtf8(
        QCryptographicHash::hash( sicnu::data::canonicalizeJsonRfc8785( json ),
                                  QCryptographicHash::Sha256 )
            .toHex() );
}

// Name fragments that must never enter a bundle, even if someone allowlists
// them by mistake.
const char *const kSecretNamePatterns[] = {
    "key", "token", "secret", "password", "passwd", "credential", "cookie", "auth",
    "session", "signature", "private",
};

// Value shapes of common credential material (defense-in-depth).
const char *const kSecretValuePatterns[] = {
    "^Bearer[ ]", "^Basic[ ]", "-----BEGIN [A-Z ]*PRIVATE KEY-----",
    "\\bAKIA[0-9A-Z]{16}\\b",      // AWS access key id
    "\\bghp_[A-Za-z0-9]{36}\\b",   // GitHub personal access token
    "\\bxox[baprs]-",              // Slack tokens
    "\\bsk-[A-Za-z0-9]{20,}\\b",   // sk- style API keys
    "\\beyJ[A-Za-z0-9_-]{10,}\\.", // JWT header segment
    "\\bgithub_pat_[A-Za-z0-9_]{20,}\\b",
};

bool nameLooksSecret( const QString &name )
{
    const QString lowered = name.toLower();
    for ( const char *pattern : kSecretNamePatterns )
    {
        if ( lowered.contains( QLatin1String( pattern ) ) )
            return true;
    }
    return false;
}

bool valueLooksSecret( const QString &value )
{
    for ( const char *pattern : kSecretValuePatterns )
    {
        if ( value.contains( QRegularExpression( QLatin1String( pattern ) ) ) )
            return true;
    }
    return false;
}

QJsonObject diffDetail( const QString &a, const QString &b )
{
    QJsonObject detail;
    detail.insert( QStringLiteral( "a" ), a );
    detail.insert( QStringLiteral( "b" ), b );
    return detail;
}

} // namespace

bool isValidRunTransition( RunStatus from, RunStatus to )
{
    if ( from == to )
        return false;
    switch ( from )
    {
        case RunStatus::Created:
            return to == RunStatus::Running || to == RunStatus::Cancelled ||
                   to == RunStatus::Failed;
        case RunStatus::Running:
            return to == RunStatus::Completed || to == RunStatus::Failed ||
                   to == RunStatus::Cancelling || to == RunStatus::Interrupted;
        case RunStatus::Cancelling:
            return to == RunStatus::Cancelled || to == RunStatus::Failed;
        case RunStatus::Interrupted:
            // Resume re-enters Running; giving up is explicit.
            return to == RunStatus::Running || to == RunStatus::Failed ||
                   to == RunStatus::Cancelled;
        case RunStatus::Cancelled:
        case RunStatus::Failed:
        case RunStatus::Completed:
            return false; // terminal; correction = new run, not a rewrite
    }
    return false;
}

bool isTerminalRunStatus( RunStatus status )
{
    return status == RunStatus::Cancelled || status == RunStatus::Failed ||
           status == RunStatus::Completed;
}

QString runConfigHash( const QJsonObject &parameters )
{
    return hashCanonical( parameters );
}

QString runExecutionFingerprint( const RunExecutionIdentity &identity )
{
    QJsonObject json;
    json.insert( QStringLiteral( "algorithm_id" ), identity.algorithmId );
    json.insert( QStringLiteral( "algorithm_version" ), identity.algorithmVersion );
    json.insert( QStringLiteral( "parameters" ), identity.parameters );
    json.insert( QStringLiteral( "dataset_version_id" ), identity.datasetVersionId );
    json.insert( QStringLiteral( "dataset_fingerprint" ), identity.datasetFingerprint );
    json.insert( QStringLiteral( "split_manifest_id" ), identity.splitManifestId );
    json.insert( QStringLiteral( "split_fingerprint" ), identity.splitFingerprint );
    json.insert( QStringLiteral( "model_digest" ), identity.modelDigest );
    json.insert( QStringLiteral( "seed_hex" ), QString::number( identity.seed, 16 ) );
    json.insert( QStringLiteral( "software_revision" ), identity.softwareRevision );
    return hashCanonical( json );
}

QString runResultFingerprint( const QStringList &artifactDigests, const QJsonObject &metrics )
{
    QJsonObject json;
    json.insert( QStringLiteral( "artifact_digests" ),
                 QJsonArray::fromStringList( artifactDigests ) );
    json.insert( QStringLiteral( "metrics" ), metrics );
    return hashCanonical( json );
}

// --- Experiment -----------------------------------------------------------------

QJsonObject Experiment::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kExperimentSerializationVersion );
    json.insert( QStringLiteral( "experiment_id" ), m_experimentId );
    json.insert( QStringLiteral( "name" ), m_name );
    if ( !m_objective.isEmpty() )
        json.insert( QStringLiteral( "objective" ), m_objective );
    if ( m_createdAtUtc.isValid() )
        json.insert( QStringLiteral( "created_at_utc" ),
                     m_createdAtUtc.toString( Qt::ISODateWithMs ) );
    if ( !m_tags.isEmpty() )
        json.insert( QStringLiteral( "tags" ), QJsonArray::fromStringList( m_tags ) );
    if ( !m_runIds.isEmpty() )
        json.insert( QStringLiteral( "run_ids" ), QJsonArray::fromStringList( m_runIds ) );
    return json;
}

Result<Experiment> Experiment::fromJson( const QJsonObject &json )
{
    using ResultT = Result<Experiment>;
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kExperimentSerializationVersion )
    {
        return ResultT::failure( Diagnostic{
            QStringLiteral( "experiment.version" ),
            QStringLiteral( "experiment payload version %1 not supported (expected %2)" )
                .arg( schemaVersion )
                .arg( kExperimentSerializationVersion ),
            DiagnosticSeverity::Error } );
    }
    Experiment experiment;
    experiment.m_experimentId = json.value( QStringLiteral( "experiment_id" ) ).toString();
    experiment.m_name = json.value( QStringLiteral( "name" ) ).toString();
    if ( experiment.m_experimentId.isEmpty() || experiment.m_name.isEmpty() )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "experiment.invalid" ),
                                             QStringLiteral( "experiment requires id + name" ),
                                             DiagnosticSeverity::Error } );
    }
    experiment.m_objective = json.value( QStringLiteral( "objective" ) ).toString();
    experiment.m_createdAtUtc = QDateTime::fromString(
        json.value( QStringLiteral( "created_at_utc" ) ).toString(), Qt::ISODateWithMs );
    experiment.m_tags = json.value( QStringLiteral( "tags" ) ).toVariant().toStringList();
    experiment.m_runIds = json.value( QStringLiteral( "run_ids" ) ).toVariant().toStringList();
    return ResultT::success( experiment );
}

// --- RunEnvironment ----------------------------------------------------------------

namespace
{

/// Environment variable allowlist: platform flags that meaningfully change
/// behavior. PATH-style machine detail is deliberately absent.
const char *const kAllowedEnvVariables[] = {
    "SICNU_ARTIFACT_CACHE", "SICNU_ARTIFACT_CACHE_DIR", "SICNU_CACHE_INPUT_DIGEST_MAX_MB",
    "SICNU_EXECUTION_CACHE", "SICNU_MCP_WORKSPACE", "SICNU_PLUGIN_UNLOAD_TIMEOUT_MS",
    "LANG", "LC_ALL", "TZ",
};

} // namespace

QHash<QString, QString> RunEnvironment::filterSecrets( const QHash<QString, QString> &variables )
{
    QHash<QString, QString> filtered;
    QHashIterator<QString, QString> it( variables );
    while ( it.hasNext() )
    {
        it.next();
        if ( nameLooksSecret( it.key() ) )
            continue;
        if ( valueLooksSecret( it.value() ) )
            continue;
        filtered.insert( it.key(), it.value() );
    }
    return filtered;
}

RunEnvironment RunEnvironment::redacted() const
{
    RunEnvironment copy( *this );
    copy.m_envVariables = filterSecrets( copy.m_envVariables );
    return copy;
}

QJsonObject RunEnvironment::redactSecretKeys( const QJsonObject &json )
{
    // Deep key-based pass (#789): export bundles serialize canonical
    // parameters verbatim, and a parameter named like a credential is a
    // credential. Values are kept for non-secret keys — parameters are
    // config, and value-shape guessing would corrupt legitimate numbers.
    QJsonObject out;
    for ( auto it = json.constBegin(); it != json.constEnd(); ++it )
    {
        const QJsonValue &value = it.value();
        if ( value.isObject() )
        {
            out.insert( it.key(), redactSecretKeys( value.toObject() ) );
            continue;
        }
        if ( value.isArray() )
        {
            QJsonArray redactedArray;
            for ( const QJsonValue &item : value.toArray() )
            {
                if ( item.isObject() )
                    redactedArray.append( redactSecretKeys( item.toObject() ) );
                else
                    redactedArray.append( item );
            }
            out.insert( it.key(), redactedArray );
            continue;
        }
        out.insert( it.key(), nameLooksSecret( it.key() ) ? QStringLiteral( "***" ) : value );
    }
    return out;
}

RunEnvironment RunEnvironment::captureCurrent()
{
    RunEnvironment environment;
    QJsonObject fields;
    fields.insert( QStringLiteral( "platform" ), QSysInfo::productType() );
    fields.insert( QStringLiteral( "platform_version" ), QSysInfo::productVersion() );
    fields.insert( QStringLiteral( "kernel" ), QSysInfo::kernelType() );
    fields.insert( QStringLiteral( "architecture" ), QSysInfo::currentCpuArchitecture() );
    fields.insert( QStringLiteral( "build_abi" ), QSysInfo::buildAbi() );
    fields.insert( QStringLiteral( "qt_version" ), QStringLiteral( QT_VERSION_STR ) );
    fields.insert( QStringLiteral( "locale" ), QLocale::system().name() );
    fields.insert( QStringLiteral( "timezone" ),
                   QString::fromUtf8( QTimeZone::systemTimeZoneId() ) );
    environment.m_fields = fields;

    const QStringList allowed = [] {
        QStringList names;
        for ( const char *name : kAllowedEnvVariables )
            names.append( QString::fromLatin1( name ) );
        return names;
    }();
    const QProcessEnvironment current = QProcessEnvironment::systemEnvironment();
    for ( const QString &name : allowed )
    {
        if ( current.contains( name ) )
            environment.m_envVariables.insert( name, current.value( name ) );
    }
    environment.m_envVariables = filterSecrets( environment.m_envVariables );
    return environment;
}

RunEnvironment RunEnvironment::fromFields( const QJsonObject &fields,
                                           const QHash<QString, QString> &envVariables )
{
    RunEnvironment environment;
    environment.m_fields = fields;
    environment.m_envVariables = filterSecrets( envVariables );
    return environment;
}

QJsonObject RunEnvironment::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "fields" ), m_fields );
    QJsonObject variables;
    QHashIterator<QString, QString> it( m_envVariables );
    while ( it.hasNext() )
    {
        it.next();
        variables.insert( it.key(), it.value() );
    }
    json.insert( QStringLiteral( "env_variables" ), variables );
    return json;
}

Result<RunEnvironment> RunEnvironment::fromJson( const QJsonObject &json )
{
    using ResultT = Result<RunEnvironment>;
    RunEnvironment environment;
    environment.m_fields = json.value( QStringLiteral( "fields" ) ).toObject();
    const QJsonObject variables = json.value( QStringLiteral( "env_variables" ) ).toObject();
    for ( auto it = variables.constBegin(); it != variables.constEnd(); ++it )
        environment.m_envVariables.insert( it.key(), it.value().toString() );
    // Defense-in-depth on LOAD too: a tampered bundle cannot smuggle secrets
    // back into the run record.
    environment.m_envVariables = filterSecrets( environment.m_envVariables );
    return ResultT::success( environment );
}

// --- ExperimentRun -------------------------------------------------------------------

RunExecutionIdentity ExperimentRun::executionIdentity() const
{
    RunExecutionIdentity identity;
    identity.algorithmId = m_algorithmId;
    identity.algorithmVersion = m_algorithmVersion;
    identity.parameters = m_parameters;
    identity.datasetVersionId = m_datasetVersionId;
    identity.datasetFingerprint = m_datasetFingerprint;
    identity.splitManifestId = m_splitManifestId;
    identity.splitFingerprint = m_splitFingerprint;
    identity.modelDigest = m_modelDigest;
    identity.seed = m_seed;
    identity.softwareRevision = m_softwareRevision;
    return identity;
}

QString ExperimentRun::resultFingerprint() const
{
    QStringList digests;
    for ( const Artifact &artifact : m_artifacts )
        digests.append( artifact.digest );
    return runResultFingerprint( digests, m_metrics );
}

QJsonObject ExperimentRun::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kExperimentSerializationVersion );
    json.insert( QStringLiteral( "run_id" ), m_runId );
    json.insert( QStringLiteral( "experiment_id" ), m_experimentId );
    json.insert( QStringLiteral( "status" ), dataset::runStatusToString( m_status ) );
    json.insert( QStringLiteral( "algorithm_id" ), m_algorithmId );
    json.insert( QStringLiteral( "algorithm_version" ), m_algorithmVersion );
    json.insert( QStringLiteral( "parameters" ), m_parameters );
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    json.insert( QStringLiteral( "dataset_fingerprint" ), m_datasetFingerprint );
    json.insert( QStringLiteral( "split_manifest_id" ), m_splitManifestId );
    json.insert( QStringLiteral( "split_fingerprint" ), m_splitFingerprint );
    if ( !m_modelId.isEmpty() )
        json.insert( QStringLiteral( "model_id" ), m_modelId );
    if ( !m_modelDigest.isEmpty() )
        json.insert( QStringLiteral( "model_digest" ), m_modelDigest );
    json.insert( QStringLiteral( "seed_hex" ), QString::number( m_seed, 16 ) );
    json.insert( QStringLiteral( "determinism" ),
                 dataset::determinismGradeToString( m_determinism ) );
    if ( !m_determinismNote.isEmpty() )
        json.insert( QStringLiteral( "determinism_note" ), m_determinismNote );
    json.insert( QStringLiteral( "environment" ), m_environment.toJson() );
    if ( !m_softwareRevision.isEmpty() )
        json.insert( QStringLiteral( "software_revision" ), m_softwareRevision );
    if ( !m_executionRef.isEmpty() )
        json.insert( QStringLiteral( "execution_ref" ), m_executionRef );
    auto stamp = []( const QDateTime &time ) {
        return time.isValid() ? time.toString( Qt::ISODateWithMs ) : QString();
    };
    json.insert( QStringLiteral( "created_at_utc" ), stamp( m_createdAtUtc ) );
    json.insert( QStringLiteral( "started_at_utc" ), stamp( m_startedAtUtc ) );
    json.insert( QStringLiteral( "finished_at_utc" ), stamp( m_finishedAtUtc ) );
    QJsonArray artifactArray;
    for ( const Artifact &artifact : m_artifacts )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "path" ), artifact.path );
        item.insert( QStringLiteral( "role" ), artifact.role );
        item.insert( QStringLiteral( "digest" ), artifact.digest );
        item.insert( QStringLiteral( "size_bytes" ), artifact.sizeBytes );
        artifactArray.append( item );
    }
    json.insert( QStringLiteral( "artifacts" ), artifactArray );
    json.insert( QStringLiteral( "metrics" ), m_metrics );
    return json;
}

Result<ExperimentRun> ExperimentRun::fromJson( const QJsonObject &json )
{
    using ResultT = Result<ExperimentRun>;
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kExperimentSerializationVersion )
    {
        return ResultT::failure( Diagnostic{
            QStringLiteral( "experiment.version" ),
            QStringLiteral( "run payload version %1 not supported (expected %2)" )
                .arg( schemaVersion )
                .arg( kExperimentSerializationVersion ),
            DiagnosticSeverity::Error } );
    }
    ExperimentRun run;
    run.m_runId = json.value( QStringLiteral( "run_id" ) ).toString();
    run.m_experimentId = json.value( QStringLiteral( "experiment_id" ) ).toString();
    if ( run.m_runId.isEmpty() || run.m_experimentId.isEmpty() )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "experiment.invalid" ),
                                             QStringLiteral( "run requires run_id + experiment_id" ),
                                             DiagnosticSeverity::Error } );
    }
    const auto status =
        dataset::runStatusFromString( json.value( QStringLiteral( "status" ) ).toString() );
    if ( !status )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "experiment.invalid" ),
                                             QStringLiteral( "run status unknown" ),
                                             DiagnosticSeverity::Error } );
    }
    run.m_status = *status;
    run.m_algorithmId = json.value( QStringLiteral( "algorithm_id" ) ).toString();
    run.m_algorithmVersion = json.value( QStringLiteral( "algorithm_version" ) ).toString();
    run.m_parameters = json.value( QStringLiteral( "parameters" ) ).toObject();
    run.m_datasetVersionId = json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    run.m_datasetFingerprint = json.value( QStringLiteral( "dataset_fingerprint" ) ).toString();
    run.m_splitManifestId = json.value( QStringLiteral( "split_manifest_id" ) ).toString();
    run.m_splitFingerprint = json.value( QStringLiteral( "split_fingerprint" ) ).toString();
    run.m_modelId = json.value( QStringLiteral( "model_id" ) ).toString();
    run.m_modelDigest = json.value( QStringLiteral( "model_digest" ) ).toString();
    {
        const QString seedHex = json.value( QStringLiteral( "seed_hex" ) ).toString();
        if ( seedHex.isEmpty() )
            run.m_seed = 0;
        else
        {
            bool ok = false;
            run.m_seed = seedHex.toULongLong( &ok, 16 );
            if ( !ok )
            {
                return ResultT::failure( Diagnostic{ QStringLiteral( "experiment.invalid" ),
                                                     QStringLiteral( "seed_hex malformed" ),
                                                     DiagnosticSeverity::Error } );
            }
        }
    }
    const auto determinism = dataset::determinismGradeFromString(
        json.value( QStringLiteral( "determinism" ) ).toString() );
    if ( !determinism )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "experiment.invalid" ),
                                             QStringLiteral( "determinism grade unknown" ),
                                             DiagnosticSeverity::Error } );
    }
    run.m_determinism = *determinism;
    run.m_determinismNote = json.value( QStringLiteral( "determinism_note" ) ).toString();
    if ( run.m_determinism != DeterminismGrade::Strict && run.m_determinismNote.isEmpty() )
    {
        return ResultT::failure( Diagnostic{
            QStringLiteral( "experiment.invalid" ),
            QStringLiteral( "non-strict determinism requires a note" ),
            DiagnosticSeverity::Error } );
    }
    const auto environment = RunEnvironment::fromJson(
        json.value( QStringLiteral( "environment" ) ).toObject() );
    if ( !environment )
        return ResultT::failure( environment.diagnostics() );
    run.m_environment = environment.value();
    run.m_softwareRevision = json.value( QStringLiteral( "software_revision" ) ).toString();
    run.m_executionRef = json.value( QStringLiteral( "execution_ref" ) ).toString();
    auto parseStamp = []( const QJsonValue &value ) {
        return QDateTime::fromString( value.toString(), Qt::ISODateWithMs );
    };
    run.m_createdAtUtc = parseStamp( json.value( QStringLiteral( "created_at_utc" ) ) );
    run.m_startedAtUtc = parseStamp( json.value( QStringLiteral( "started_at_utc" ) ) );
    run.m_finishedAtUtc = parseStamp( json.value( QStringLiteral( "finished_at_utc" ) ) );
    for ( const QJsonValue &value : json.value( QStringLiteral( "artifacts" ) ).toArray() )
    {
        const QJsonObject item = value.toObject();
        Artifact artifact;
        artifact.path = item.value( QStringLiteral( "path" ) ).toString();
        artifact.role = item.value( QStringLiteral( "role" ) ).toString();
        artifact.digest = item.value( QStringLiteral( "digest" ) ).toString();
        artifact.sizeBytes = item.value( QStringLiteral( "size_bytes" ) ).toInt( -1 );
        run.m_artifacts.append( artifact );
    }
    run.m_metrics = json.value( QStringLiteral( "metrics" ) ).toObject();

    // A run may not resurrect itself into a non-terminal state, and a
    // terminal run must carry a finished stamp.
    if ( isTerminalRunStatus( run.m_status ) && !run.m_finishedAtUtc.isValid() )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "experiment.invalid" ),
                                             QStringLiteral( "terminal run without finish time" ),
                                             DiagnosticSeverity::Error } );
    }
    return ResultT::success( run );
}

// --- Comparison -------------------------------------------------------------------------

QJsonObject RunDiffItem::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "dimension" ), dimension );
    json.insert( QStringLiteral( "differs" ), differs );
    json.insert( QStringLiteral( "detail" ), detail );
    return json;
}

RunComparison RunComparison::compare( const ExperimentRun &a, const ExperimentRun &b )
{
    RunComparison comparison;
    auto add = [&]( const QString &dimension, bool differs, const QString &detail ) {
        RunDiffItem item;
        item.dimension = dimension;
        item.differs = differs;
        item.detail = detail;
        comparison.dimensions.append( item );
        if ( differs )
            comparison.reasons.append( detail );
    };

    // Identity pins first (goal §27): dataset, split, model.
    const bool datasetDiffers =
        a.datasetVersionId() != b.datasetVersionId() ||
        a.datasetFingerprint() != b.datasetFingerprint();
    add( QStringLiteral( "dataset" ), datasetDiffers,
         datasetDiffers ? QStringLiteral( "dataset version or fingerprint differs" )
                        : QStringLiteral( "identical" ) );
    const bool splitDiffers =
        a.splitManifestId() != b.splitManifestId() ||
        a.splitFingerprint() != b.splitFingerprint();
    add( QStringLiteral( "split" ), splitDiffers,
         splitDiffers ? QStringLiteral( "split manifest or fingerprint differs" )
                      : QStringLiteral( "identical" ) );
    const bool modelDiffers = a.modelDigest() != b.modelDigest();
    add( QStringLiteral( "model" ), modelDiffers,
         modelDiffers ? QStringLiteral( "model digest differs" ) : QStringLiteral( "identical" ) );
    const bool algorithmDiffers =
        a.algorithmId() != b.algorithmId() || a.algorithmVersion() != b.algorithmVersion();
    add( QStringLiteral( "algorithm" ), algorithmDiffers,
         algorithmDiffers ? QStringLiteral( "algorithm/workflow identity differs" )
                          : QStringLiteral( "identical" ) );
    const bool configDiffers = a.configHash() != b.configHash();
    add( QStringLiteral( "config" ), configDiffers,
         configDiffers ? QStringLiteral( "canonical parameter hash differs" )
                       : QStringLiteral( "identical" ) );
    const bool seedDiffers = a.seed() != b.seed();
    add( QStringLiteral( "seed" ), seedDiffers,
         seedDiffers ? QStringLiteral( "seed differs" ) : QStringLiteral( "identical" ) );
    const bool environmentDiffers = !( a.environment() == b.environment() );
    add( QStringLiteral( "environment" ), environmentDiffers,
         environmentDiffers ? QStringLiteral( "environment snapshot differs" )
                            : QStringLiteral( "identical" ) );

    // M6 diagnostics: artifact identity (digest sets) and runtime (wall
    // time). Artifacts differing under identical identity pins mean the
    // outputs themselves must be diffed before numbers are trusted.
    auto digestSet = []( const ExperimentRun &run ) {
        QStringList digests;
        for ( const ExperimentRun::Artifact &artifact : run.artifacts() )
        {
            if ( !artifact.digest.isEmpty() )
                digests.append( artifact.digest );
        }
        std::sort( digests.begin(), digests.end() );
        return digests;
    };
    const bool artifactsMissing = a.artifacts().isEmpty() || b.artifacts().isEmpty();
    const bool artifactsDiffer = digestSet( a ) != digestSet( b );
    QString artifactDetail = QStringLiteral( "identical" );
    if ( artifactsMissing )
        artifactDetail = QStringLiteral( "artifact evidence missing on one side" );
    else if ( artifactsDiffer )
        artifactDetail = QStringLiteral( "output digest sets differ" );
    add( QStringLiteral( "artifacts" ), artifactsDiffer, artifactDetail );

    const auto wallMs = []( const ExperimentRun &run ) {
        if ( !run.startedAtUtc().isValid() || !run.finishedAtUtc().isValid() )
            return qint64( -1 );
        return run.startedAtUtc().msecsTo( run.finishedAtUtc() );
    };
    const qint64 aMs = wallMs( a );
    const qint64 bMs = wallMs( b );
    const bool runtimeMissing = aMs < 0 || bMs < 0;
    // Wall time "differs" only beyond max(1s, 1% of the slower run) — small
    // scheduler jitter is not a scientific difference.
    const bool runtimeDiffers =
        runtimeMissing ||
        ( std::llabs( aMs - bMs ) > qMax<qint64>( 1000, qMax<qint64>( aMs, bMs ) / 100 ) );
    QString runtimeDetail = QStringLiteral( "identical" );
    if ( runtimeMissing )
        runtimeDetail = QStringLiteral( "timing evidence missing on one side" );
    else if ( runtimeDiffers )
        runtimeDetail = QStringLiteral( "wall time differs beyond 1%: %1 ms vs %2 ms" )
                            .arg( aMs )
                            .arg( bMs );
    add( QStringLiteral( "runtime" ), runtimeDiffers, runtimeDetail );

    if ( datasetDiffers || splitDiffers || modelDiffers )
        comparison.verdict = Verdict::NotComparable;
    else if ( algorithmDiffers || configDiffers || seedDiffers || environmentDiffers )
        comparison.verdict = Verdict::ComparableWithDifferences;
    else
        comparison.verdict = Verdict::Comparable;
    return comparison;
}

QJsonObject RunComparison::toJson() const
{
    QJsonObject json;
    QJsonArray items;
    for ( const RunDiffItem &item : dimensions )
        items.append( item.toJson() );
    json.insert( QStringLiteral( "dimensions" ), items );
    json.insert( QStringLiteral( "reasons" ), QJsonArray::fromStringList( reasons ) );
    json.insert( QStringLiteral( "verdict" ),
                 verdict == Verdict::Comparable
                     ? QStringLiteral( "comparable" )
                     : ( verdict == Verdict::ComparableWithDifferences
                             ? QStringLiteral( "comparable_with_differences" )
                             : QStringLiteral( "not_comparable" ) ) );
    return json;
}

RunComparison RunComparison::fromJson( const QJsonObject &json )
{
    RunComparison comparison;
    const QString verdict = json.value( QStringLiteral( "verdict" ) ).toString();
    comparison.verdict = verdict == QLatin1String( "not_comparable" )
                             ? Verdict::NotComparable
                             : ( verdict == QLatin1String( "comparable_with_differences" )
                                     ? Verdict::ComparableWithDifferences
                                     : Verdict::Comparable );
    for ( const QJsonValue &value : json.value( QStringLiteral( "dimensions" ) ).toArray() )
    {
        const QJsonObject item = value.toObject();
        RunDiffItem diff;
        diff.dimension = item.value( QStringLiteral( "dimension" ) ).toString();
        diff.differs = item.value( QStringLiteral( "differs" ) ).toBool();
        diff.detail = item.value( QStringLiteral( "detail" ) ).toString();
        comparison.dimensions.append( diff );
    }
    comparison.reasons = json.value( QStringLiteral( "reasons" ) ).toVariant().toStringList();
    return comparison;
}

QJsonObject RunComparison::metricDiff( const ExperimentRun &a, const ExperimentRun &b ) const
{
    QJsonObject diff;
    const QJsonObject aMetrics = a.metrics();
    const QJsonObject bMetrics = b.metrics();
    for ( auto it = aMetrics.constBegin(); it != aMetrics.constEnd(); ++it )
    {
        if ( !it.value().isDouble() )
            continue;
        const auto other = bMetrics.constFind( it.key() );
        if ( other == bMetrics.constEnd() || !other.value().isDouble() )
            continue;
        QJsonObject entry;
        entry.insert( QStringLiteral( "a" ), it.value().toDouble() );
        entry.insert( QStringLiteral( "b" ), other.value().toDouble() );
        entry.insert( QStringLiteral( "delta" ),
                      other.value().toDouble() - it.value().toDouble() );
        diff.insert( it.key(), entry );
    }
    return diff;
}

// --- Promotion evidence ------------------------------------------------------------

QJsonObject PromotionRecord::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "promotion_id" ), promotionId );
    json.insert( QStringLiteral( "run_id" ), runId );
    json.insert( QStringLiteral( "model_id" ), modelId );
    json.insert( QStringLiteral( "model_digest" ), modelDigest );
    json.insert( QStringLiteral( "dataset_version_id" ), datasetVersionId );
    json.insert( QStringLiteral( "verdict" ), verdict );
    json.insert( QStringLiteral( "decision" ), decision );
    json.insert( QStringLiteral( "decided_by" ), decidedBy );
    if ( decidedAtUtc.isValid() )
        json.insert( QStringLiteral( "decided_at_utc" ),
                     decidedAtUtc.toString( Qt::ISODateWithMs ) );
    if ( !criteriaJson.isEmpty() )
    {
        json.insert( QStringLiteral( "criteria" ),
                     QJsonDocument::fromJson( criteriaJson.toUtf8() ).object() );
    }
    if ( createdAtUtc.isValid() )
        json.insert( QStringLiteral( "created_at_utc" ),
                     createdAtUtc.toString( Qt::ISODateWithMs ) );
    return json;
}

Result<PromotionRecord> PromotionRecord::fromJson( const QJsonObject &json )
{
    PromotionRecord record;
    record.promotionId = json.value( QStringLiteral( "promotion_id" ) ).toString();
    record.runId = json.value( QStringLiteral( "run_id" ) ).toString();
    record.modelId = json.value( QStringLiteral( "model_id" ) ).toString();
    record.modelDigest = json.value( QStringLiteral( "model_digest" ) ).toString();
    record.datasetVersionId = json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    record.verdict = json.value( QStringLiteral( "verdict" ) ).toString();
    record.decision = json.value( QStringLiteral( "decision" ) ).toString( QStringLiteral( "pending" ) );
    record.decidedBy = json.value( QStringLiteral( "decided_by" ) ).toString();
    record.decidedAtUtc = QDateTime::fromString(
        json.value( QStringLiteral( "decided_at_utc" ) ).toString(), Qt::ISODateWithMs );
    const QJsonObject criteria = json.value( QStringLiteral( "criteria" ) ).toObject();
    if ( !criteria.isEmpty() )
    {
        // CANONICAL form (Compact) — equality of stored records compares
        // this string; formatting drift must never fake a conflict.
        record.criteriaJson = QString::fromUtf8(
            QJsonDocument( criteria ).toJson( QJsonDocument::Compact ) );
    }
    record.createdAtUtc = QDateTime::fromString(
        json.value( QStringLiteral( "created_at_utc" ) ).toString(), Qt::ISODateWithMs );
    if ( record.promotionId.isEmpty() || record.runId.isEmpty() )
        return Result<PromotionRecord>::failure(
            Diagnostic{ QStringLiteral( "experiment.promotion_invalid" ),
                        QStringLiteral( "promotion record requires promotion_id + run_id" ),
                        DiagnosticSeverity::Error } );
    return Result<PromotionRecord>::success( record );
}

} // namespace sicnu::experiment
