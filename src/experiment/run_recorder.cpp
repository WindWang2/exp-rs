// run_recorder.cpp — see run_recorder.h for the contract.
#include "run_recorder.h"
#include <QJsonArray>

#include "../dataset/dataset_store.h"

#include <QUuid>

namespace sicnu::experiment
{

namespace
{

using sicnu::dataset::Diagnostic;
using sicnu::dataset::DiagnosticSeverity;

Diagnostic recorderDiag( QString code, QString message )
{
    return Diagnostic{ std::move( code ), std::move( message ), DiagnosticSeverity::Error };
}

template <typename T>
Result<T> failWith( const QString &code, const QString &message )
{
    return Result<T>::failure( recorderDiag( code, message ) );
}

using VoidResult = Result<void>;

VoidResult failVoid( const QString &code, const QString &message )
{
    return VoidResult::failure( recorderDiag( code, message ) );
}

QString newRunId()
{
    return QUuid::createUuid().toString( QUuid::WithoutBraces );
}

} // namespace

ExperimentRunRecorder::ExperimentRunRecorder( ExperimentStore &store )
    : m_store( &store )
{
}

void ExperimentRunRecorder::setDatasetStore( const sicnu::dataset::DatasetStore *store )
{
    m_datasetStore = store;
}

Result<ExperimentRun> ExperimentRunRecorder::loadRun( const QString &runId ) const
{
    const auto run = m_store->runById( runId );
    if ( !run )
        return Result<ExperimentRun>::failure(
            recorderDiag( QStringLiteral( "experiment.run_not_found" ),
                          QStringLiteral( "run %1 is not in the store" ).arg( runId ) ) );
    return Result<ExperimentRun>::success( run.value() );
}

Result<QString> ExperimentRunRecorder::startRun( const RunStartRequest &request )
{
    if ( request.experimentId.isEmpty() )
        return failWith<QString>( QStringLiteral( "experiment.recorder_missing_experiment" ),
                                  QStringLiteral( "run request carries no experiment id" ) );

    const bool knownExperiment =
        m_store->experimentById( request.experimentId ).has_value();
    if ( !knownExperiment )
        return failWith<QString>(
            QStringLiteral( "experiment.recorder_missing_experiment" ),
            QStringLiteral( "experiment %1 is not in the store" ).arg( request.experimentId ) );

    ExperimentRun run;
    run.setRunId( newRunId() );
    run.setExperimentId( request.experimentId );
    run.setStatus( RunStatus::Created );
    run.setAlgorithmId( request.algorithmId );
    run.setAlgorithmVersion( request.algorithmVersion );
    run.setParameters( request.parameters );
    run.setDatasetVersionId( request.datasetVersionId );
    run.setDatasetFingerprint( request.datasetFingerprint );
    run.setSplitManifestId( request.splitManifestId );
    run.setSplitFingerprint( request.splitFingerprint );
    run.setModelId( request.modelId );
    run.setModelDigest( request.modelDigest );
    run.setSeed( request.seed );
    run.setDeterminism( request.determinism );
    run.setDeterminismNote( request.determinismNote );
    run.setSoftwareRevision( request.softwareRevision );
    run.setExecutionRef( request.executionRef );
    run.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );

    // Environment: capture when the caller did not supply one, and ALWAYS
    // persist the redacted copy (defense-in-depth at the record boundary).
    RunEnvironment environment =
        request.environment.fields().isEmpty() && request.environment.envVariables().isEmpty()
            ? RunEnvironment::captureCurrent()
            : request.environment;
    run.setEnvironment( environment.redacted() );

    // Dataset pin verification/fill: a run must never pin a version that is
    // not in the authoritative store when the store is wired.
    if ( !request.datasetVersionId.isEmpty() )
    {
        const auto versionId =
            sicnu::dataset::DatasetVersionId::fromString( request.datasetVersionId );
        if ( m_datasetStore )
        {
            const auto record =
                versionId ? m_datasetStore->versionById( versionId.value() ) : std::nullopt;
            if ( !record )
                return failWith<QString>(
                    QStringLiteral( "experiment.recorder_missing_version" ),
                    QStringLiteral( "dataset version %1 is not in the store" )
                        .arg( request.datasetVersionId ) );
            if ( run.datasetFingerprint().isEmpty() )
                run.setDatasetFingerprint( record->fingerprint() );
        }
        else if ( !versionId )
        {
            return failWith<QString>(
                QStringLiteral( "experiment.recorder_missing_version" ),
                QStringLiteral( "dataset version id '%1' does not parse" )
                    .arg( request.datasetVersionId ) );
        }
    }

    if ( request.determinism != DeterminismGrade::Strict && request.determinismNote.isEmpty() )
        return failWith<QString>( QStringLiteral( "experiment.determinism_note_required" ),
                                  QStringLiteral( "non-strict determinism requires a note" ) );

    auto created = m_store->upsertRun( run );
    if ( !created )
        return Result<QString>::failure( created.diagnostics() );

    // Created → Running in the same recorder call: the record exists exactly
    // when the execution is about to be live.
    run.setStatus( RunStatus::Running );
    run.setStartedAtUtc( QDateTime::currentDateTimeUtc() );
    auto running = m_store->upsertRun( run );
    if ( !running )
        return Result<QString>::failure( running.diagnostics() );
    return Result<QString>::success( run.runId() );
}

VoidResult ExperimentRunRecorder::markSucceeded( const QString &runId,
                                                 const QVector<ExperimentRun::Artifact> &artifacts,
                                                 const QJsonObject &metrics )
{
    auto loaded = loadRun( runId );
    if ( !loaded )
        return VoidResult::failure( loaded.diagnostics() );
    ExperimentRun run = loaded.value();
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    run.artifacts() = artifacts;
    // MERGE (not replace): lifecycle evidence recorded earlier on the same
    // record (interrupt_note from a resumed execution, extra evidence)
    // survives success; the completion document overwrites only its own keys.
    QJsonObject merged = run.metrics();
    const QJsonObject additions = RunEnvironment::redactSecretKeys( metrics );
    for ( auto it = additions.begin(); it != additions.end(); ++it )
        merged.insert( it.key(), it.value() );
    run.setMetrics( merged );
    auto written = m_store->upsertRun( run );
    if ( !written )
        return VoidResult::failure( written.diagnostics() );
    return VoidResult::success();
}

VoidResult ExperimentRunRecorder::markFailed( const QString &runId, const QString &errorCode,
                                              const QString &message )
{
    auto loaded = loadRun( runId );
    if ( !loaded )
        return VoidResult::failure( loaded.diagnostics() );
    ExperimentRun run = loaded.value();
    run.setStatus( RunStatus::Failed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    QJsonObject evidence;
    evidence.insert( QStringLiteral( "error_code" ), errorCode );
    evidence.insert( QStringLiteral( "message" ), message );
    QJsonObject metrics = run.metrics();
    metrics.insert( QStringLiteral( "error" ), evidence );
    run.setMetrics( RunEnvironment::redactSecretKeys( metrics ) );
    auto written = m_store->upsertRun( run );
    if ( !written )
        return VoidResult::failure( written.diagnostics() );
    return VoidResult::success();
}

VoidResult ExperimentRunRecorder::markCancelled( const QString &runId, const QString &reason )
{
    auto loaded = loadRun( runId );
    if ( !loaded )
        return VoidResult::failure( loaded.diagnostics() );
    ExperimentRun run = loaded.value();
    // Running → Cancelling → Cancelled is the store's legal path; a run that
    // already sits in Cancelling/Interrupted re-enters this single-step.
    if ( run.status() == RunStatus::Running )
    {
        run.setStatus( RunStatus::Cancelling );
        auto cancelling = m_store->upsertRun( run );
        if ( !cancelling )
            return VoidResult::failure( cancelling.diagnostics() );
    }
    run.setStatus( RunStatus::Cancelled );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    QJsonObject metrics = run.metrics();
    metrics.insert( QStringLiteral( "cancel_reason" ), reason );
    run.setMetrics( RunEnvironment::redactSecretKeys( metrics ) );
    auto written = m_store->upsertRun( run );
    if ( !written )
        return VoidResult::failure( written.diagnostics() );
    return VoidResult::success();
}

VoidResult ExperimentRunRecorder::markInterrupted( const QString &runId, const QString &note )
{
    auto loaded = loadRun( runId );
    if ( !loaded )
        return VoidResult::failure( loaded.diagnostics() );
    ExperimentRun run = loaded.value();
    if ( run.status() == RunStatus::Interrupted )
    {
        // Idempotent re-delivery: merge the newest note, never a transition
        // (Interrupted → Interrupted is not a state change).
        if ( note.isEmpty() )
            return VoidResult::success();
        QJsonObject metrics = run.metrics();
        metrics.insert( QStringLiteral( "interrupt_note" ), note );
        run.setMetrics( RunEnvironment::redactSecretKeys( metrics ) );
        auto written = m_store->upsertRun( run );
        if ( !written )
            return VoidResult::failure( written.diagnostics() );
        return VoidResult::success();
    }
    // The store validates the transition (only a Running run may become
    // Interrupted; a Created run was never live, so it cannot be interrupted).
    run.setStatus( RunStatus::Interrupted );
    QJsonObject metrics = run.metrics();
    metrics.insert( QStringLiteral( "interrupt_note" ), note );
    run.setMetrics( RunEnvironment::redactSecretKeys( metrics ) );
    auto written = m_store->upsertRun( run );
    if ( !written )
        return VoidResult::failure( written.diagnostics() );
    return VoidResult::success();
}

VoidResult ExperimentRunRecorder::markResumed( const QString &runId )
{
    auto loaded = loadRun( runId );
    if ( !loaded )
        return VoidResult::failure( loaded.diagnostics() );
    ExperimentRun run = loaded.value();
    if ( run.status() == RunStatus::Running )
        return VoidResult::success(); // idempotent re-delivery
    run.setStatus( RunStatus::Running );
    auto written = m_store->upsertRun( run );
    if ( !written )
        return VoidResult::failure( written.diagnostics() );
    return VoidResult::success();
}

VoidResult ExperimentRunRecorder::recordMetrics( const QString &runId,
                                                 const EvaluationProtocol &protocol,
                                                 const QJsonObject &metrics )
{
    const auto run = m_store->runById( runId );
    if ( !run )
        return failVoid( QStringLiteral( "experiment.run_not_found" ),
                         QStringLiteral( "run %1 is not in the store" ).arg( runId ) );
    MetricRecord record;
    record.runId = runId;
    record.protocol = protocol;
    record.metrics = RunEnvironment::redactSecretKeys( metrics );
    auto written = m_store->saveMetricRecord( record );
    if ( !written )
        return VoidResult::failure( written.diagnostics() );
    return VoidResult::success();
}

QVector<ExperimentRunRecorder::StaleRun> ExperimentRunRecorder::reconcileStaleRuns(
    const QSet<QString> &liveExecutionRefs ) const
{
    QVector<StaleRun> stale;
    // Bounded page walk: listRuns pages by kMaxPageSize and we only inspect
    // non-terminal statuses; the scan stays linear in stored runs.
    qint64 offset = 0;
    while ( true )
    {
        const auto page =
            m_store->listRuns( QString(), QString(), QString(), offset,
                               ExperimentStore::kMaxPageSize );
        if ( !page || page.value().second.isEmpty() )
            break;
        for ( const auto &run : page.value().second )
        {
            if ( isTerminalRunStatus( run.status() ) )
                continue;
            if ( liveExecutionRefs.contains( run.executionRef() ) )
                continue;
            StaleRun item;
            item.runId = run.runId();
            item.status = run.status();
            item.executionRef = run.executionRef();
            item.startedAtUtc = run.startedAtUtc();
            stale.append( item );
        }
        offset += page.value().second.size();
    }
    return stale;
}

} // namespace sicnu::experiment
