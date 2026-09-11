// run_bridge.cpp — see run_bridge.h for the contract.
#include "run_bridge.h"

#include "../dataset/dataset_store.h"
#include "../dataset/split.h"

#include <QDateTime>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QUuid>

namespace sicnu::experiment
{

namespace
{

using sicnu::dataset::Diagnostic;
using sicnu::dataset::DiagnosticSeverity;

constexpr int kBridgedStepLimit = 256;
constexpr int kMaxLiveRefs = 10000;

Diagnostic bridgeDiag( QString code, QString message )
{
    return Diagnostic{ std::move( code ), std::move( message ), DiagnosticSeverity::Error };
}

template <typename T>
Result<T> failWith( const QString &code, const QString &message )
{
    return Result<T>::failure( bridgeDiag( code, message ) );
}

using VoidResult = Result<void>;

VoidResult failVoid( const QString &code, const QString &message )
{
    return VoidResult::failure( bridgeDiag( code, message ) );
}


} // namespace

// --- RunPins -----------------------------------------------------------------

QJsonObject RunPins::toJson() const
{
    QJsonObject json;
    if ( !datasetVersionId.isEmpty() )
        json.insert( QStringLiteral( "dataset_version_id" ), datasetVersionId );
    if ( !splitManifestId.isEmpty() )
        json.insert( QStringLiteral( "split_manifest_id" ), splitManifestId );
    if ( !modelId.isEmpty() )
        json.insert( QStringLiteral( "model_id" ), modelId );
    if ( !modelDigest.isEmpty() )
        json.insert( QStringLiteral( "model_digest" ), modelDigest );
    if ( hasSeed )
        json.insert( QStringLiteral( "seed" ), qint64( seed ) );
    return json;
}

Result<RunPins> RunPins::fromJson( const QJsonObject &json )
{
    RunPins pins;
    pins.datasetVersionId = json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    pins.splitManifestId = json.value( QStringLiteral( "split_manifest_id" ) ).toString();
    pins.modelId = json.value( QStringLiteral( "model_id" ) ).toString();
    pins.modelDigest = json.value( QStringLiteral( "model_digest" ) ).toString();
    if ( json.contains( QStringLiteral( "seed" ) ) )
    {
        const QJsonValue value = json.value( QStringLiteral( "seed" ) );
        if ( !value.isDouble() )
            return failWith<RunPins>( QStringLiteral( "experiment.bridge_invalid_pins" ),
                                      QStringLiteral( "seed must be a number" ) );
        const qreal raw = value.toDouble();
        if ( raw < 0 )
            return failWith<RunPins>( QStringLiteral( "experiment.bridge_invalid_pins" ),
                                      QStringLiteral( "seed must be non-negative" ) );
        pins.seed = static_cast<quint64>( raw );
        pins.hasSeed = true;
    }
    return Result<RunPins>::success( pins );
}

bool RunPins::isEmpty() const
{
    return datasetVersionId.isEmpty() && splitManifestId.isEmpty() && modelId.isEmpty()
           && modelDigest.isEmpty() && !hasSeed;
}

// --- ExperimentRunBridge -------------------------------------------------------

ExperimentRunBridge::ExperimentRunBridge( ExperimentStore &store )
    : m_store( &store )
    , m_recorder( store )
{
}

void ExperimentRunBridge::setDatasetStore( const sicnu::dataset::DatasetStore *store )
{
    QMutexLocker lock( &m_mutex );
    m_recorder.setDatasetStore( store );
    m_datasetStore = store;
}

Result<void> ExperimentRunBridge::ensureExperiment( const QString &experimentId,
                                                    const QString &name,
                                                    const QString &objective )
{
    QMutexLocker lock( &m_mutex );
    if ( experimentId.isEmpty() )
        return failVoid( QStringLiteral( "experiment.bridge_invalid_event" ),
                         QStringLiteral( "experiment id must not be empty" ) );
    if ( m_store->experimentById( experimentId ) )
    {
        m_experimentId = experimentId;
        return VoidResult::success();
    }
    Experiment experiment;
    experiment.setExperimentId( experimentId );
    experiment.setName( name.isEmpty() ? experimentId : name );
    experiment.setObjective( objective );
    experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    auto written = m_store->upsertExperiment( experiment );
    if ( !written )
        return VoidResult::failure( written.diagnostics() );
    m_experimentId = experimentId;
    return VoidResult::success();
}

void ExperimentRunBridge::setWorkflowPins( const QString &workflowId, const RunPins &pins )
{
    if ( workflowId.isEmpty() )
        return;
    QMutexLocker lock( &m_mutex );
    if ( pins.isEmpty() )
        m_pinsByWorkflow.remove( workflowId );
    else
        m_pinsByWorkflow.insert( workflowId, pins );
}

Result<void> ExperimentRunBridge::attachExecutionPins( const QString &executionRef,
                                                       const RunPins &pins )
{
    if ( executionRef.isEmpty() )
        return failVoid( QStringLiteral( "experiment.bridge_invalid_event" ),
                         QStringLiteral( "execution ref must not be empty" ) );
    QMutexLocker lock( &m_mutex );

    const QString runId = m_runIdByExecution.value( executionRef );
    if ( runId.isEmpty() )
    {
        // Not started yet (or not in this session): park as the per-execution
        // override; handleExecutionEvent(Running) resolves it at start time.
        if ( pins.isEmpty() )
            m_pinsByExecution.remove( executionRef );
        else
            m_pinsByExecution.insert( executionRef, pins );
        return VoidResult::success();
    }

    // Mutable on purpose: pins are merged into the fetched copy and written
    // back through upsertRun below (const would reject the setters — a
    // build break on origin/master that this compat fix unblocks).
    auto run = m_store->runById( runId );
    if ( !run )
        return failVoid( QStringLiteral( "experiment.run_not_found" ),
                         QStringLiteral( "run %1 is not in the store" ).arg( runId ) );
    if ( run->status() == RunStatus::Created )
    {
        // Still un-started: pins may be merged into the record directly.
        if ( !pins.datasetVersionId.isEmpty() )
            run->setDatasetVersionId( pins.datasetVersionId );
        if ( !pins.splitManifestId.isEmpty() )
            run->setSplitManifestId( pins.splitManifestId );
        if ( !pins.modelId.isEmpty() )
            run->setModelId( pins.modelId );
        if ( !pins.modelDigest.isEmpty() )
            run->setModelDigest( pins.modelDigest );
        if ( pins.hasSeed )
            run->setSeed( pins.seed );
        auto written = m_store->upsertRun( run.value() );
        if ( !written )
            return VoidResult::failure( written.diagnostics() );
        return VoidResult::success();
    }

    // The run already started: identity pins are immutable (store-enforced).
    // Equal pins are an idempotent success; differing pins are an honest
    // refusal — the caller must register them BEFORE the run starts.
    RunExecutionIdentity identity = run->executionIdentity();
    bool conflicts = false;
    if ( !pins.datasetVersionId.isEmpty() && pins.datasetVersionId != identity.datasetVersionId )
        conflicts = true;
    if ( !pins.splitManifestId.isEmpty() && pins.splitManifestId != identity.splitManifestId )
        conflicts = true;
    if ( !pins.modelDigest.isEmpty() && pins.modelDigest != identity.modelDigest )
        conflicts = true;
    if ( pins.hasSeed && pins.seed != identity.seed )
        conflicts = true;
    if ( conflicts )
        return failVoid( QStringLiteral( "experiment.bridge_pins_late" ),
                         QStringLiteral( "execution %1 already started with different identity "
                                         "pins; register pins before the run starts" )
                             .arg( executionRef ) );
    return VoidResult::success();
}

RunPins ExperimentRunBridge::pinsForExecutionLocked( const QString &executionRef,
                                                     const QString &workflowId ) const
{
    RunPins pins;
    const auto workflowPins = m_pinsByWorkflow.constFind( workflowId );
    if ( workflowPins != m_pinsByWorkflow.constEnd() )
        pins = workflowPins.value();
    const auto executionPins = m_pinsByExecution.constFind( executionRef );
    if ( executionPins != m_pinsByExecution.constEnd() )
    {
        const RunPins &override = executionPins.value();
        if ( !override.datasetVersionId.isEmpty() )
            pins.datasetVersionId = override.datasetVersionId;
        if ( !override.splitManifestId.isEmpty() )
            pins.splitManifestId = override.splitManifestId;
        if ( !override.modelId.isEmpty() )
            pins.modelId = override.modelId;
        if ( !override.modelDigest.isEmpty() )
            pins.modelDigest = override.modelDigest;
        if ( override.hasSeed )
        {
            pins.seed = override.seed;
            pins.hasSeed = true;
        }
    }
    return pins;
}

RunPins ExperimentRunBridge::pinsForExecution( const QString &executionRef,
                                               const QString &workflowId ) const
{
    QMutexLocker lock( &m_mutex );
    return pinsForExecutionLocked( executionRef, workflowId );
}

QJsonObject ExperimentRunBridge::workflowEvidence( const ExecutionEvent &event )
{
    QJsonObject evidence;
    evidence.insert( QStringLiteral( "execution_ref" ), event.executionRef );
    if ( !event.workflowId.isEmpty() )
        evidence.insert( QStringLiteral( "workflow_id" ), event.workflowId );
    evidence.insert( QStringLiteral( "reported_state" ), event.state );
    if ( event.startedMs > 0 )
        evidence.insert( QStringLiteral( "started_ms" ), event.startedMs );
    if ( event.finishedMs > 0 )
        evidence.insert( QStringLiteral( "finished_ms" ), event.finishedMs );
    if ( !event.steps.isEmpty() )
    {
        QJsonArray steps;
        const int count = qMin( event.steps.size(), kBridgedStepLimit );
        for ( int i = 0; i < count; ++i )
            steps.append( event.steps.at( i ) );
        evidence.insert( QStringLiteral( "steps" ), steps );
        if ( count < event.steps.size() )
            evidence.insert( QStringLiteral( "steps_truncated" ), true );
    }
    if ( !event.artifacts.isEmpty() )
        evidence.insert( QStringLiteral( "artifact_paths" ), event.artifacts );
    if ( !event.errorMessage.isEmpty() )
        evidence.insert( QStringLiteral( "error_message" ), event.errorMessage );
    if ( !event.extra.isEmpty() )
        evidence.insert( QStringLiteral( "extra" ), event.extra );
    return evidence;
}

QString ExperimentRunBridge::resolveRunId( const QString &executionRef ) const
{
    const QString mapped = m_runIdByExecution.value( executionRef );
    if ( !mapped.isEmpty() )
        return mapped;
    // Cold path (bridge attached after a restart): bounded store scan.
    const QStringList ids = m_store->runIdsByExecutionRef( executionRef );
    return ids.isEmpty() ? QString() : ids.first();
}

Result<QString> ExperimentRunBridge::startFromEvent( const ExecutionEvent &event,
                                                     const RunPins &pins )
{
    RunStartRequest request;
    request.experimentId = m_experimentId;
    // The executed "algorithm" of an auto-recorded run is the workflow; the
    // definition has no version of its own, so the field stays honestly empty.
    request.algorithmId = !event.workflowId.isEmpty() ? event.workflowId : event.executionRef;
    // The workflow definition IS the configuration of the execution.
    request.parameters = event.definition;
    request.datasetVersionId = pins.datasetVersionId;
    request.splitManifestId = pins.splitManifestId;
    request.modelId = pins.modelId;
    request.modelDigest = pins.modelDigest;
    if ( pins.hasSeed )
        request.seed = pins.seed;
    request.executionRef = event.executionRef;

    // Split fingerprint: verified from the authoritative store when both the
    // pin and a dataset store are available — never invented.
    if ( !pins.splitManifestId.isEmpty() && m_datasetStore )
    {
        const auto manifest = m_datasetStore->splitManifestById( pins.splitManifestId );
        if ( manifest )
            request.splitFingerprint = manifest->fingerprint();
        // A pin that does not resolve stays verifiable-by-absence: the
        // manifest id is recorded, the fingerprint stays empty; replay
        // readiness reports the gap instead of a fake ok.
    }

    return m_recorder.startRun( request );
}

Result<QString> ExperimentRunBridge::handleExecutionEvent( const ExecutionEvent &event )
{
    if ( !event.isValid() )
        return failWith<QString>( QStringLiteral( "experiment.bridge_invalid_event" ),
                                  QStringLiteral( "event carries no execution ref" ) );
    if ( !bridgeExecutionStates().contains( event.state ) )
        return failWith<QString>(
            QStringLiteral( "experiment.bridge_invalid_event" ),
            QStringLiteral( "unknown execution state '%1' — the bridge never guesses a mapping" )
                .arg( event.state ) );
    if ( m_experimentId.isEmpty() )
        return failWith<QString>( QStringLiteral( "experiment.bridge_no_target" ),
                                  QStringLiteral( "bridge has no target experiment" ) );

    QMutexLocker lock( &m_mutex );

    if ( event.state == QStringLiteral( "Running" ) )
    {
        const QString existing = resolveRunId( event.executionRef );
        if ( !existing.isEmpty() )
        {
            // Late re-delivery of a start for an already-terminal run is a
            // tolerated no-op (the store knows more than this event says).
            const auto current = m_store->runById( existing );
            if ( current && isTerminalRunStatus( current->status() ) )
                return Result<QString>::success( existing );
            // Otherwise markResumed is a no-op for a Running run and
            // re-opens an Interrupted one (resume continuation).
            auto resumed = m_recorder.markResumed( existing );
            if ( !resumed )
                return Result<QString>::failure( resumed.diagnostics() );
            return Result<QString>::success( existing );
        }
        const RunPins pins = pinsForExecutionLocked( event.executionRef, event.workflowId );
        auto started = startFromEvent( event, pins );
        if ( !started )
            return started;
        // Bound the in-process map: a bridge that records hundreds of
        // thousands of executions in one process does not need its oldest
        // warm entries (they fall back to the store's cold-path scan).
        if ( m_runIdByExecution.size() >= kMaxLiveRefs )
            m_runIdByExecution.clear();
        m_runIdByExecution.insert( event.executionRef, started.value() );
        m_pinsByExecution.remove( event.executionRef );
        return started;
    }

    // Terminal / interruption events require a run this bridge knows about:
    // silently backfilling a start would fabricate history.
    const QString runId = resolveRunId( event.executionRef );
    if ( runId.isEmpty() )
        return failWith<QString>(
            QStringLiteral( "experiment.bridge_unknown_execution" ),
            QStringLiteral( "%1 event for execution %2, which this bridge never saw start" )
                .arg( event.state, event.executionRef ) );

    const auto current = m_store->runById( runId );
    if ( !current )
        return failWith<QString>( QStringLiteral( "experiment.run_not_found" ),
                                  QStringLiteral( "run %1 is not in the store" ).arg( runId ) );

    const QString completed = QStringLiteral( "Completed" );
    const QString failed = QStringLiteral( "Failed" );
    const QString canceled = QStringLiteral( "Canceled" );

    if ( event.state == completed )
    {
        if ( current->status() == RunStatus::Completed )
            return Result<QString>::success( runId ); // idempotent re-delivery
        if ( current->status() == RunStatus::Interrupted )
        {
            // The execution plane reports completion for a record that last
            // reported Interrupted: the only truthful path between them is a
            // resume (Interrupted → Running → Completed). The coordinator
            // does not re-emit Running for the original id after a resume
            // swap, so the bridge advances explicitly — never a direct
            // Interrupted → Completed jump.
            auto resumed = m_recorder.markResumed( runId );
            if ( !resumed )
                return Result<QString>::failure( resumed.diagnostics() );
        }
        QVector<ExperimentRun::Artifact> artifacts;
        const QStringList stepIds = event.artifacts.keys();
        artifacts.reserve( stepIds.size() );
        for ( const QString &stepId : stepIds )
        {
            const QJsonObject entry = event.artifacts.value( stepId ).toObject();
            ExperimentRun::Artifact artifact;
            artifact.path = entry.value( QStringLiteral( "path" ) ).toString();
            // For auto-recorded workflow runs the artifact role carries the
            // producing step id: the record answers "which step made this".
            artifact.role = stepId;
            artifact.digest = entry.value( QStringLiteral( "digest" ) ).toString();
            artifact.sizeBytes = static_cast<qint64>(
                entry.value( QStringLiteral( "size" ) ).toDouble( -1 ) );
            artifacts.append( artifact );
        }
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "workflow" ), workflowEvidence( event ) );
        auto succeeded = m_recorder.markSucceeded( runId, artifacts, metrics );
        if ( !succeeded )
            return Result<QString>::failure( succeeded.diagnostics() );
        return Result<QString>::success( runId );
    }

    if ( event.state == failed )
    {
        if ( current->status() == RunStatus::Failed )
            return Result<QString>::success( runId ); // idempotent re-delivery
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "workflow" ), workflowEvidence( event ) );
        auto recorded = m_recorder.markFailed(
            runId, QStringLiteral( "workflow.failed" ),
            event.errorMessage.isEmpty()
                ? QStringLiteral( "execution reported Failed (no error detail supplied)" )
                : event.errorMessage );
        if ( !recorded )
            return Result<QString>::failure( recorded.diagnostics() );
        return Result<QString>::success( runId );
    }

    if ( event.state == canceled )
    {
        if ( current->status() == RunStatus::Cancelled )
            return Result<QString>::success( runId ); // idempotent re-delivery
        auto recorded = m_recorder.markCancelled(
            runId,
            event.errorMessage.isEmpty()
                ? QStringLiteral( "execution reported Canceled" )
                : event.errorMessage );
        if ( !recorded )
            return Result<QString>::failure( recorded.diagnostics() );
        return Result<QString>::success( runId );
    }

    // Interrupted: non-terminal; a later resume continues the same record.
    if ( current->status() == RunStatus::Interrupted )
        return Result<QString>::success( runId ); // idempotent re-delivery
    auto interrupted = m_recorder.markInterrupted(
        runId,
        event.errorMessage.isEmpty()
            ? QStringLiteral( "execution reported Interrupted" )
            : event.errorMessage );
    if ( !interrupted )
        return Result<QString>::failure( interrupted.diagnostics() );
    return Result<QString>::success( runId );
}

QVector<ExperimentRunBridge::StaleDecision> ExperimentRunBridge::reconcileStale(
    const QSet<QString> &liveExecutionRefs, const CheckpointLookup &checkpointLookup )
{
    QMutexLocker lock( &m_mutex );
    QVector<StaleDecision> decisions;
    const auto stale = m_recorder.reconcileStaleRuns( liveExecutionRefs );
    for ( const auto &item : stale )
    {
        StaleDecision decision;
        decision.runId = item.runId;
        decision.executionRef = item.executionRef;

        std::optional<ExecutionEvidence> evidence;
        if ( checkpointLookup )
            evidence = checkpointLookup( item.executionRef );
        if ( evidence && evidence->liveOwner )
        {
            // Another live process owns this execution: its outcome belongs
            // to that process's recording path, never to our reconciliation.
            decision.action = QStringLiteral( "live" );
            decision.detail = QStringLiteral( "execution is owned by a live process; skipped" );
            decisions.append( decision );
            continue;
        }
        if ( !evidence || !evidence->present )
        {
            // No checkpoint evidence at all: the outcome cannot be proven, so
            // the record is reported, never rewritten to a guessed state.
            decision.action = QStringLiteral( "report" );
            decision.detail = QStringLiteral( "no checkpoint evidence available" );
            decisions.append( decision );
            continue;
        }

        if ( evidence->state == QStringLiteral( "Failed" ) )
        {
            auto closed = m_recorder.markFailed(
                item.runId, QStringLiteral( "workflow.stale_reconciled" ),
                QStringLiteral( "checkpoint state: Failed" ) );
            decision.action = closed ? QStringLiteral( "failed" )
                                     : QStringLiteral( "report" );
            decision.detail = closed ? QStringLiteral( "closed Failed from checkpoint evidence" )
                                     : QStringLiteral( "close Failed failed: %1" )
                                           .arg( closed.diagnostics().first().message );
        }
        else if ( evidence->state == QStringLiteral( "Canceled" ) )
        {
            auto closed = m_recorder.markCancelled(
                item.runId, QStringLiteral( "checkpoint state: Canceled" ) );
            decision.action = closed ? QStringLiteral( "cancelled" )
                                     : QStringLiteral( "report" );
            decision.detail = closed
                                  ? QStringLiteral( "closed Cancelled from checkpoint evidence" )
                                  : QStringLiteral( "close Cancelled failed: %1" )
                                        .arg( closed.diagnostics().first().message );
        }
        else if ( evidence->state == QStringLiteral( "Interrupted" ) )
        {
            // Non-terminal: a resumed execution may still complete the story.
            auto marked = m_recorder.markInterrupted(
                item.runId, QStringLiteral( "checkpoint state: Interrupted" ) );
            decision.action = marked ? QStringLiteral( "interrupted" )
                                     : QStringLiteral( "report" );
            decision.detail = marked
                                  ? QStringLiteral( "marked Interrupted from checkpoint evidence" )
                                  : QStringLiteral( "mark Interrupted failed: %1" )
                                        .arg( marked.diagnostics().first().message );
        }
        else if ( evidence->state == QStringLiteral( "Completed" ) )
        {
            // A completed checkpoint proves completion but this path carries
            // no artifact digests — closing the run "succeeded" here would
            // fabricate output evidence. Reported for explicit follow-up.
            decision.action = QStringLiteral( "report" );
            decision.detail = QStringLiteral(
                "checkpoint completed but no artifact evidence in scope; close "
                "through the normal recording path instead" );
        }
        else
        {
            // Running/Ready/... on disk: the previous owner died before any
            // recovery pass — still resumable, never auto-closed.
            decision.action = QStringLiteral( "interrupted" );
            decision.detail = QStringLiteral( "checkpoint state %1 is non-terminal; left "
                                              "resumable" )
                                  .arg( evidence->state );
            auto marked = m_recorder.markInterrupted(
                item.runId, QStringLiteral( "checkpoint state: %1" ).arg( evidence->state ) );
            if ( !marked )
            {
                decision.action = QStringLiteral( "report" );
                decision.detail = QStringLiteral( "mark Interrupted failed: %1" )
                                      .arg( marked.diagnostics().first().message );
            }
        }
        decisions.append( decision );
    }
    return decisions;
}

QString ExperimentRunBridge::runIdForExecution( const QString &executionRef ) const
{
    if ( executionRef.isEmpty() )
        return QString();
    QMutexLocker lock( &m_mutex );
    return resolveRunId( executionRef );
}

} // namespace sicnu::experiment
