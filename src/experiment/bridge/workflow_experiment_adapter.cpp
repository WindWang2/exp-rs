// workflow_experiment_adapter.cpp — see workflow_experiment_adapter.h.
#include "workflow_experiment_adapter.h"

#include "execution_event_conversion.h"

#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"
#include "workflow/workflow_run_lock.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QThread>
#include <QtLogging>

#include <json/json.h>

namespace sicnu::experiment
{

// The WorkflowRun → ExecutionEvent projection (and its bounded jsoncpp →
// QJson helpers) lives in execution_event_conversion.cpp so every recording
// surface produces byte-identical evidence through the same code path.

// --- WorkflowExperimentMonitor -------------------------------------------------

WorkflowExperimentMonitor::WorkflowExperimentMonitor( workflow::WorkflowRunCoordinator &coordinator,
                                                      QObject *parent )
    : QObject( parent )
    , m_coordinator( coordinator )
{
}

WorkflowExperimentMonitor::~WorkflowExperimentMonitor()
{
    if ( m_connected )
        disconnect( &m_coordinator, nullptr, this, nullptr );
}

QString WorkflowExperimentMonitor::checkpointPathFor( const QString &runId ) const
{
    // Same file the coordinator writes (checkpointPathLocked) and the same
    // name WorkflowCheckpointManager archives under <dir>/history/.
    return m_coordinator.checkpointDirectory() + QDir::separator()
           + QStringLiteral( "checkpoint_%1.json" ).arg( runId );
}

bool WorkflowExperimentMonitor::enable( const QString &experimentDbPath,
                                        const QString &experimentId,
                                        const QString &experimentName,
                                        const QString &objective,
                                        const QString &datasetDbPath, QString *error )
{
    Q_ASSERT( thread() == QThread::currentThread() );
    if ( experimentDbPath.isEmpty() || experimentId.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "experiment db path and experiment id are required" );
        return false;
    }
    // One experiment db per monitor: a second enable() with a DIFFERENT path
    // would silently record into the first db — refuse instead.
    if ( !m_experimentDbPath.isEmpty() && m_experimentDbPath != experimentDbPath )
    {
        if ( error )
            *error = QStringLiteral( "monitor already bound to experiment db %1" )
                         .arg( m_experimentDbPath );
        return false;
    }
    if ( !m_store.isOpen() && !m_store.open( experimentDbPath, error ) )
        return false;
    m_experimentDbPath = experimentDbPath;

    if ( !m_bridge )
        m_bridge = std::make_unique<ExperimentRunBridge>( m_store );

    if ( datasetDbPath.isEmpty() )
    {
        // An absent dataset db disables pin VERIFICATION for subsequent
        // submissions; it never keeps a stale store from an earlier call.
        m_datasetStore.reset();
    }
    else if ( !m_datasetStore )
    {
        m_datasetStore = std::make_unique<sicnu::dataset::DatasetStore>();
        if ( !m_datasetStore->open( datasetDbPath, error ) )
            m_datasetStore.reset();
    }

    m_bridge->setDatasetStore( m_datasetStore.get() );
    const auto ensured = m_bridge->ensureExperiment( experimentId, experimentName, objective );
    if ( !ensured )
    {
        if ( error )
            *error = ensured.diagnostics().first().message;
        return false;
    }

    if ( !m_connected )
    {
        // The coordinator emits with its internal mutex held: queued delivery
        // only (mirrors the governance mirror wiring in ProjectContext).
        connect( &m_coordinator, &workflow::WorkflowRunCoordinator::runStateChanged, this,
                 &WorkflowExperimentMonitor::onRunStateChanged, Qt::QueuedConnection );
        m_connected = true;
    }

    // Interrupted/stale reconciliation where supported (goal 8.0 §A): at
    // enable time, runs recorded non-terminal whose execution is provably
    // dead get their truthful close; live owners (this process or elsewhere)
    // are never touched. Bounded: only non-terminal records are inspected.
    reconcileStaleRuns();
    return true;
}

bool WorkflowExperimentMonitor::isEnabled() const
{
    return m_bridge && !m_bridge->targetExperiment().isEmpty();
}

void WorkflowExperimentMonitor::setWorkflowPins( const QString &workflowId, const RunPins &pins )
{
    if ( m_bridge )
        m_bridge->setWorkflowPins( workflowId, pins );
}

Result<QString> WorkflowExperimentMonitor::recordSubmission( const workflow::WorkflowRun &run,
                                                             const RunPins &pins )
{
    Q_ASSERT( thread() == QThread::currentThread() );
    if ( !m_bridge )
    {
        return Result<QString>::failure( Diagnostic{
            QStringLiteral( "experiment.bridge_no_target" ),
            QStringLiteral( "monitor is not enabled" ), DiagnosticSeverity::Error } );
    }
    const QString runId = QString::fromStdString( run.runId() );
    // Per-submission opt-in: only this run's lifecycle is recorded.
    m_enabledRefs.insert( runId );
    // Pins bind to THIS run before the start transition lands — immune to
    // queued-signal reordering or later submissions of the same workflow.
    if ( !pins.isEmpty() )
    {
        const auto attached = m_bridge->attachExecutionPins( runId, pins );
        if ( !attached )
            return Result<QString>::failure( attached.diagnostics() );
    }
    const ExecutionEvent event =
        workflowRunToExecutionEvent( run, QStringLiteral( "Running" ), 0, 0 );
    return m_bridge->handleExecutionEvent( event );
}

void WorkflowExperimentMonitor::optInResume( const QString &executionRef )
{
    Q_ASSERT( thread() == QThread::currentThread() );
    if ( !executionRef.isEmpty() )
        m_enabledRefs.insert( executionRef );
}

Result<QString> WorkflowExperimentMonitor::recordAggregateState(
    const workflow::WorkflowRun &run )
{
    Q_ASSERT( thread() == QThread::currentThread() );
    if ( !m_bridge )
    {
        return Result<QString>::failure( Diagnostic{
            QStringLiteral( "experiment.bridge_no_target" ),
            QStringLiteral( "monitor is not enabled" ), DiagnosticSeverity::Error } );
    }
    const QString runId = QString::fromStdString( run.runId() );
    if ( !m_enabledRefs.contains( runId ) )
    {
        return Result<QString>::failure( Diagnostic{
            QStringLiteral( "experiment.bridge_unknown_execution" ),
            QStringLiteral( "execution %1 was never enabled for recording" ).arg( runId ),
            DiagnosticSeverity::Error } );
    }
    const QString state = QString::fromStdString( workflowRunStateToString( run.state() ) );
    // Transitional states carry no experiment meaning: ignoring records
    // nothing and fabricates nothing (same rule as the signal path).
    if ( !bridgeExecutionStates().contains( state ) )
    {
        return Result<QString>::failure( Diagnostic{
            QStringLiteral( "experiment.bridge_invalid_event" ),
            QStringLiteral( "state %1 is transitional; nothing recorded" ).arg( state ),
            DiagnosticSeverity::Error } );
    }
    const ExecutionEvent event = workflowRunToExecutionEvent( run, state, 0, 0 );
    return m_bridge->handleExecutionEvent( event );
}

QVector<ExperimentRunBridge::StaleDecision> WorkflowExperimentMonitor::reconcileStaleRuns()
{
    QVector<ExperimentRunBridge::StaleDecision> decisions;
    if ( !m_bridge )
        return decisions;

    // Live executions = this process's tracked runs + any execution whose run
    // lock is held by a live process anywhere (cross-process protection — a
    // run owned elsewhere is never closed behind its owner's back).
    QSet<QString> live;
    for ( const auto &run : m_coordinator.runs() )
    {
        if ( run && !workflow::isTerminalRunState( run->state() ) )
            live.insert( QString::fromStdString( run->runId() ) );
    }

    const QString directory = m_coordinator.checkpointDirectory();
    auto checkpointLookup = [&live, &directory]( const QString &executionRef )
        -> std::optional<ExecutionEvidence> {
        if ( executionRef.isEmpty() )
            return std::nullopt;

        // Flock probe: acquiring proves no live owner exists anywhere.
        workflow::WorkflowRunLock probe(
            workflow::WorkflowRunLock::lockPathForRun( directory, executionRef.toStdString() ) );
        QString heldByPid;
        const workflow::WorkflowRunLock::TryResult acquired = probe.tryAcquire( &heldByPid );
        if ( acquired == workflow::WorkflowRunLock::TryResult::HeldByLiveOwner )
            return ExecutionEvidence{ executionRef, QString(), false, /*liveOwner=*/true };
        if ( acquired != workflow::WorkflowRunLock::TryResult::Acquired )
        {
            // Lock file could not be created/locked (deleted dir, …): the
            // outcome cannot be probed — report, never close.
            return std::nullopt;
        }
        // The probe now owns the lock — release immediately so a concurrent
        // recovery/resume in another process is not blocked by a stale probe.
        probe.release();

        QString path = directory + QDir::separator()
                       + QStringLiteral( "checkpoint_%1.json" ).arg( executionRef );
        if ( !QFile::exists( path ) )
        {
            // Completed runs are archived under <dir>/history/ (same
            // filename, same convention as WorkflowCheckpointManager).
            path = directory + QDir::separator() + QStringLiteral( "history" )
                   + QDir::separator()
                   + QStringLiteral( "checkpoint_%1.json" ).arg( executionRef );
            if ( !QFile::exists( path ) )
                return std::nullopt;
        }
        workflow::WorkflowCheckpointManager manager;
        QString loadError;
        auto run = manager.loadCheckpoint( path, &loadError );
        if ( !run )
            return std::nullopt; // corrupt/unreadable: report, never guess
        return ExecutionEvidence{ executionRef,
                                  QString::fromStdString( workflow::workflowRunStateToString(
                                      run->state() ) ),
                                  /*present=*/true, /*liveOwner=*/false };
    };

    decisions = m_bridge->reconcileStale( live, checkpointLookup );
    for ( const auto &decision : decisions )
    {
        qWarning( "experiment recording: stale reconciliation %s (%s): %s",
                  qUtf8Printable( decision.executionRef ), qUtf8Printable( decision.action ),
                  qUtf8Printable( decision.detail ) );
    }
    return decisions;
}

void WorkflowExperimentMonitor::flush()
{
    if ( !m_bridge )
        return;
    // Drain queued runStateChanged meta-calls targeted at THIS object without
    // spinning unrelated work (test/CLI shutdown paths may have no loop).
    QCoreApplication::sendPostedEvents( this, QEvent::MetaCall );
}

void WorkflowExperimentMonitor::onRunStateChanged( const QString &runId,
                                                   const QString &workflowId,
                                                   const QString &state, qint64 startedMs,
                                                   qint64 finishedMs )
{
    if ( !m_bridge )
        return;
    // Per-submission scope: submissions that never opted in are never
    // recorded (enabling the monitor is not consent for the whole process).
    if ( !m_enabledRefs.contains( runId ) )
        return;
    if ( !bridgeExecutionStates().contains( state ) )
        return; // transitional workflow state: no experiment meaning

    const long pipelineId = m_coordinator.pipelineIdForRun( runId.toStdString() );
    if ( pipelineId <= 0 && state == QStringLiteral( "Running" ) )
    {
        // An untracked Running event for an ENABLED ref is ambiguous: either
        // a resume-swap ghost or a real registration race. The checkpoint
        // disambiguates: real runs persist theirs BEFORE dispatch, ghosts
        // lose theirs at the resume swap. (Terminal/Interrupted events are
        // always recorded — startup recovery reconciles from disk, not from
        // the live map.)
        if ( !QFile::exists( checkpointPathFor( runId ) ) )
            return;
    }

    ExecutionEvent event;
    event.executionRef = runId;
    event.workflowId = workflowId;
    event.state = state;
    event.startedMs = startedMs;
    event.finishedMs = finishedMs;

    // Enrich from the authoritative run aggregate when it is still tracked
    // (terminal runs stay registered, so this answers for the whole story).
    if ( pipelineId > 0 )
    {
        if ( const auto run = m_coordinator.runForPipeline( pipelineId ) )
            event = workflowRunToExecutionEvent( *run, state, startedMs, finishedMs );
    }

    const auto recorded = m_bridge->handleExecutionEvent( event );
    if ( !recorded )
    {
        qWarning( "experiment recording: %s event for %s not recorded: %s",
                  qUtf8Printable( state ), qUtf8Printable( runId ),
                  qUtf8Printable( recorded.diagnostics().first().message ) );
    }
}

} // namespace sicnu::experiment
