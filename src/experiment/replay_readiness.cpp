// replay_readiness.cpp — see replay_readiness.h for the contract.
#include "replay_readiness.h"
#include <QJsonArray>

#include "../dataset/dataset_store.h"
#include "../dataset/split.h"

#include <QFileInfo>

namespace sicnu::experiment
{

namespace
{

using sicnu::dataset::Diagnostic;
using sicnu::dataset::ReproductionLevel;

void addDatasetCheck( QVector<ReplayCheck> &checks, const ExperimentRun &run,
                      const sicnu::dataset::DatasetStore *datasetStore )
{
    ReplayCheck check;
    check.dependency = QStringLiteral( "dataset_version" );
    if ( run.datasetVersionId().isEmpty() )
    {
        check.status = ReplayCheckStatus::Missing;
        check.detail = QStringLiteral( "run records no dataset version" );
    }
    else
    {
        const auto versionId =
            sicnu::dataset::DatasetVersionId::fromString( run.datasetVersionId() );
        const auto record =
            datasetStore && versionId
                ? datasetStore->versionById( versionId.value() )
                : std::optional<sicnu::dataset::DatasetVersionRecord>{};
        if ( !record )
        {
            check.status = datasetStore ? ReplayCheckStatus::Missing : ReplayCheckStatus::Unknown;
            check.detail = datasetStore
                               ? QStringLiteral( "version %1 is not in the store" )
                                     .arg( run.datasetVersionId() )
                               : QStringLiteral( "no dataset store wired" );
        }
        else if ( !run.datasetFingerprint().isEmpty() &&
                  record->fingerprint() != run.datasetFingerprint() )
        {
            check.status = ReplayCheckStatus::Mismatched;
            check.detail = QStringLiteral( "fingerprint %1 != recorded %2" )
                               .arg( record->fingerprint(), run.datasetFingerprint() );
        }
        else
        {
            check.status = ReplayCheckStatus::Ok;
            check.detail = record->fingerprint();
        }
    }
    checks.append( check );
}

void addSplitCheck( QVector<ReplayCheck> &checks, const ExperimentRun &run,
                    const sicnu::dataset::DatasetStore *datasetStore )
{
    ReplayCheck check;
    check.dependency = QStringLiteral( "split_manifest" );
    if ( run.splitManifestId().isEmpty() )
    {
        check.status = ReplayCheckStatus::Missing;
        check.detail = QStringLiteral( "run records no split manifest" );
    }
    else
    {
        const auto manifest =
            datasetStore ? datasetStore->splitManifestById( run.splitManifestId() )
                         : std::optional<sicnu::dataset::SplitManifest>{};
        if ( !manifest )
        {
            check.status = datasetStore ? ReplayCheckStatus::Missing : ReplayCheckStatus::Unknown;
            check.detail = datasetStore
                               ? QStringLiteral( "manifest %1 is not in the store" )
                                     .arg( run.splitManifestId() )
                               : QStringLiteral( "no dataset store wired" );
        }
        else if ( !run.splitFingerprint().isEmpty() &&
                  manifest->fingerprint() != run.splitFingerprint() )
        {
            check.status = ReplayCheckStatus::Mismatched;
            check.detail = QStringLiteral( "fingerprint %1 != recorded %2" )
                               .arg( manifest->fingerprint(), run.splitFingerprint() );
        }
        else
        {
            check.status = ReplayCheckStatus::Ok;
            check.detail = manifest->fingerprint();
        }
    }
    checks.append( check );
}

void addArtifactChecks( QVector<ReplayCheck> &checks, const ExperimentRun &run,
                        const ReproductionHooks &hooks )
{
    for ( const auto &artifact : run.artifacts() )
    {
        ReplayCheck check;
        check.dependency = QStringLiteral( "artifact:%1" ).arg( artifact.path );
        if ( hooks.artifactAvailable )
        {
            const bool available = hooks.artifactAvailable( artifact.path, artifact.sizeBytes );
            check.status = available ? ReplayCheckStatus::Ok : ReplayCheckStatus::Missing;
            check.detail = available ? QStringLiteral( "file present" )
                                     : QStringLiteral( "file missing or size mismatched" );
        }
        else
        {
            // Fall back to a plain existence probe; without the recorded size
            // check this stays honest but weaker.
            const QFileInfo info( artifact.path );
            check.status = info.exists() ? ReplayCheckStatus::Ok : ReplayCheckStatus::Missing;
            check.detail = info.exists() ? QStringLiteral( "file present (existence only)" )
                                         : QStringLiteral( "file missing" );
        }
        checks.append( check );
    }
}

void addModelAndAlgorithmChecks( QVector<ReplayCheck> &checks, const ExperimentRun &run,
                                 const ReproductionHooks &hooks )
{
    if ( !run.modelId().isEmpty() )
    {
        ReplayCheck check;
        check.dependency = QStringLiteral( "model" );
        if ( hooks.modelAvailable )
        {
            const bool available = hooks.modelAvailable( run.modelId(), run.modelDigest() );
            check.status = available ? ReplayCheckStatus::Ok : ReplayCheckStatus::Missing;
            check.detail = available ? QStringLiteral( "resolvable with matching digest" )
                                     : QStringLiteral( "not resolvable or digest changed" );
        }
        else
        {
            check.status = ReplayCheckStatus::Unknown;
            check.detail = QStringLiteral( "no model catalog hook wired" );
        }
        checks.append( check );
    }
    {
        // The algorithm pin is REQUIRED: an absent pin means nothing was
        // recorded about the executing algorithm, so a replay can never be
        // verified. Silence here would overstate the level to Exact.
        ReplayCheck check;
        check.dependency = QStringLiteral( "algorithm" );
        if ( run.algorithmId().isEmpty() )
        {
            check.status = ReplayCheckStatus::Missing;
            check.detail = QStringLiteral( "run records no algorithm" );
        }
        else if ( hooks.algorithmAvailable )
        {
            const bool available = hooks.algorithmAvailable( run.algorithmId() );
            check.status = available ? ReplayCheckStatus::Ok : ReplayCheckStatus::Missing;
            check.detail = available ? QStringLiteral( "executable in this install" )
                                     : QStringLiteral( "not executable in this install" );
        }
        else
        {
            check.status = ReplayCheckStatus::Unknown;
            check.detail = QStringLiteral( "no operator/workflow registry hook wired" );
        }
        checks.append( check );
    }
}

} // namespace

QString replayCheckStatusToString( ReplayCheckStatus status )
{
    switch ( status )
    {
        case ReplayCheckStatus::Ok:
            return QStringLiteral( "ok" );
        case ReplayCheckStatus::Missing:
            return QStringLiteral( "missing" );
        case ReplayCheckStatus::Mismatched:
            return QStringLiteral( "mismatched" );
        case ReplayCheckStatus::Unknown:
            return QStringLiteral( "unknown" );
    }
    return QStringLiteral( "unknown" );
}

QJsonObject ReplayCheck::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "dependency" ), dependency );
    json.insert( QStringLiteral( "status" ), replayCheckStatusToString( status ) );
    json.insert( QStringLiteral( "detail" ), detail );
    return json;
}

QStringList ReplayReadinessReport::missingDependencyDiagnostics() const
{
    QStringList diagnostics;
    for ( const auto &check : checks )
    {
        if ( check.status == ReplayCheckStatus::Missing ||
             check.status == ReplayCheckStatus::Mismatched )
            diagnostics.append(
                QStringLiteral( "%1: %2 (%3)" )
                    .arg( check.dependency, check.detail,
                          replayCheckStatusToString( check.status ) ) );
    }
    return diagnostics;
}

QJsonObject ReplayReadinessReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "level" ),
                 sicnu::dataset::reproductionLevelToString( level ) );
    QJsonArray checkArray;
    for ( const auto &check : checks )
        checkArray.append( check.toJson() );
    json.insert( QStringLiteral( "checks" ), checkArray );
    json.insert( QStringLiteral( "missing" ),
                 QJsonArray::fromStringList( missingDependencyDiagnostics() ) );
    json.insert( QStringLiteral( "notes" ), QJsonArray::fromStringList( notes ) );
    return json;
}

ReplayReadinessReport ReplayReadiness::assess( const ExperimentRun &run,
                                               const sicnu::dataset::DatasetStore *datasetStore,
                                               const ReproductionHooks &hooks )
{
    ReplayReadinessReport report;
    addDatasetCheck( report.checks, run, datasetStore );
    addSplitCheck( report.checks, run, datasetStore );
    addArtifactChecks( report.checks, run, hooks );
    addModelAndAlgorithmChecks( report.checks, run, hooks );

    bool blocking = false;
    bool unknown = false;
    for ( const auto &check : report.checks )
    {
        if ( check.status == ReplayCheckStatus::Missing ||
             check.status == ReplayCheckStatus::Mismatched )
            blocking = true;
        else if ( check.status == ReplayCheckStatus::Unknown )
            unknown = true;
    }
    if ( blocking )
        report.level = ReproductionLevel::Impossible;
    else if ( unknown )
        report.level = ReproductionLevel::BestEffort;
    else
        report.level = ReproductionLevel::Exact;
    return report;
}

QStringList ReplayReadiness::equivalentRuns( const ExperimentStore &store,
                                             const QString &fingerprint,
                                             const QString &excludeRunId )
{
    QStringList equivalent;
    // Bounded page walk over all runs; comparison is by recorded identity.
    qint64 offset = 0;
    while ( true )
    {
        const auto page =
            store.listRuns( QString(), QString(), QString(), offset, ExperimentStore::kMaxPageSize );
        if ( !page || page.value().second.isEmpty() )
            break;
        for ( const auto &run : page.value().second )
        {
            if ( run.runId() == excludeRunId )
                continue;
            if ( runExecutionFingerprint( run.executionIdentity() ) == fingerprint )
                equivalent.append( run.runId() );
        }
        offset += page.value().second.size();
    }
    return equivalent;
}

} // namespace sicnu::experiment
