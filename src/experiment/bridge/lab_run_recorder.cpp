// lab_run_recorder.cpp — see lab_run_recorder.h.
#include "lab_run_recorder.h"

#include "execution_event_conversion.h"

#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run_coordinator.h"
#include "workflow/workflow_run_lock.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QtLogging>

namespace sicnu::experiment
{

namespace
{
/// ADR 0143 bounded-evidence budget, applied to the operation-trail snapshot
/// (the bridge caps steps the same way).
constexpr int kOperationTrailLimit = 256;
} // namespace

LabRunRecorder::LabRunRecorder( workflow::WorkflowRunCoordinator &coordinator, QObject *parent )
    : QObject( parent )
    , m_coordinator( coordinator )
{
    // Shutdown safety: queued terminal events must still land while the
    // store is open — the app's event loop may not pump again after quit.
    if ( QCoreApplication::instance() )
    {
        connect( QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                 &LabRunRecorder::flush, Qt::UniqueConnection );
    }
}

LabRunRecorder::~LabRunRecorder()
{
    if ( m_connected )
        disconnect( &m_coordinator, nullptr, this, nullptr );
}

bool LabRunRecorder::enable( const QString &experimentDbPath, const QString &experimentId,
                             const QString &experimentName, const QString &objective,
                             const QString &datasetDbPath, QString *error )
{
    if ( experimentDbPath.isEmpty() || experimentId.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "experiment db path and experiment id are required" );
        return false;
    }
    if ( !m_experimentDbPath.isEmpty() && m_experimentDbPath != experimentDbPath )
    {
        if ( error )
            *error = QStringLiteral( "lab recorder already bound to experiment db %1" )
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
        // An absent dataset db disables pin VERIFICATION; it never keeps a
        // stale store from an earlier call.
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
    m_bridge->setTargetExperiment( experimentId );

    if ( !m_connected )
    {
        connect( &m_coordinator, &workflow::WorkflowRunCoordinator::runStateChanged, this,
                 &LabRunRecorder::onRunStateChanged, Qt::QueuedConnection );
        m_connected = true;
    }
    m_enabled = true;

    // Non-terminal records whose execution is provably dead get their
    // truthful close (same policy as the monitor's enable-time reconcile);
    // executions owned by any live process are never touched.
    QSet<QString> live;
    for ( const auto &run : m_coordinator.runs() )
    {
        if ( run && !workflow::isTerminalRunState( run->state() ) )
            live.insert( QString::fromStdString( run->runId() ) );
    }
    const auto decisions =
        m_bridge->reconcileStale( live, {} /* no checkpoint evidence source here */ );
    for ( const auto &decision : decisions )
    {
        qWarning( "lab recording: stale reconciliation %s (%s): %s",
                  qUtf8Printable( decision.executionRef ), qUtf8Printable( decision.action ),
                  qUtf8Printable( decision.detail ) );
    }
    return true;
}

bool LabRunRecorder::isBound() const
{
    return m_bridge != nullptr && m_store.isOpen();
}

void LabRunRecorder::setRecordingEnabled( bool enabled )
{
    m_enabled = enabled;
}

bool LabRunRecorder::recordingEnabled() const
{
    return m_enabled;
}

void LabRunRecorder::setWorkflowPins( const QString &workflowId, const RunPins &pins )
{
    if ( m_bridge )
        m_bridge->setWorkflowPins( workflowId, pins );
}

Result<void> LabRunRecorder::attachExecutionPins( const QString &executionRef,
                                                  const RunPins &pins )
{
    if ( !m_bridge )
    {
        return Result<void>::failure( Diagnostic{
            QStringLiteral( "lab.recorder_not_bound" ),
            QStringLiteral( "lab recorder is not bound to an experiment store" ),
            sicnu::dataset::DiagnosticSeverity::Error } );
    }
    return m_bridge->attachExecutionPins( executionRef, pins );
}

void LabRunRecorder::setOperationTrailSource( OperationTrailSource source )
{
    m_trailSource = std::move( source );
}

QString LabRunRecorder::experimentId() const
{
    return m_bridge ? m_bridge->targetExperiment() : QString();
}

QString LabRunRecorder::experimentDbPath() const
{
    return m_experimentDbPath;
}

ExperimentStore *LabRunRecorder::store()
{
    return m_store.isOpen() ? &m_store : nullptr;
}

QString LabRunRecorder::runIdForExecution( const QString &executionRef ) const
{
    return m_bridge ? m_bridge->runIdForExecution( executionRef ) : QString();
}

QStringList LabRunRecorder::recordedExecutionRefs() const
{
    return m_trackedOrder;
}

void LabRunRecorder::flush()
{
    if ( !m_bridge )
        return;
    QCoreApplication::sendPostedEvents( this, QEvent::MetaCall );
}

QString LabRunRecorder::checkpointPathFor( const QString &runId ) const
{
    return m_coordinator.checkpointDirectory() + QDir::separator()
           + QStringLiteral( "checkpoint_%1.json" ).arg( runId );
}

void LabRunRecorder::onRunStateChanged( const QString &runId, const QString &workflowId,
                                        const QString &state, qint64 startedMs,
                                        qint64 finishedMs )
{
    if ( !m_bridge || !isBound() )
        return;
    if ( !bridgeExecutionStates().contains( state ) )
        return; // transitional workflow state: no experiment meaning

    const bool running = state == QStringLiteral( "Running" );
    if ( running )
    {
        if ( !m_enabled || m_trackedRefs.contains( runId ) )
            return; // opt-out, or this story already started
        // Ghost suppression (inherited from the monitor): an untracked
        // Running event without a persisted checkpoint is a resume-swap
        // ghost or a registration race — the checkpoint disambiguates.
        const long pipelineId = m_coordinator.pipelineIdForRun( runId.toStdString() );
        if ( pipelineId <= 0 && !QFile::exists( checkpointPathFor( runId ) ) )
            return;
        m_trackedRefs.insert( runId );
        m_trackedOrder << runId;
    }
    else if ( !m_trackedRefs.contains( runId ) )
    {
        return; // terminal/Interrupted for an execution we never started
    }

    ExecutionEvent event;
    event.executionRef = runId;
    event.workflowId = workflowId;
    event.state = state;
    event.startedMs = startedMs;
    event.finishedMs = finishedMs;

    // Enrich from the authoritative run aggregate while it is still tracked
    // (terminal runs stay registered, so this answers for the whole story).
    const long pipelineId = m_coordinator.pipelineIdForRun( runId.toStdString() );
    if ( pipelineId > 0 )
    {
        if ( const auto run = m_coordinator.runForPipeline( pipelineId ) )
            event = workflowRunToExecutionEvent( *run, state, startedMs, finishedMs );
    }

    // Operation-trail evidence: redacted + capped, injected only on
    // terminal/Interrupted events (the story's end — the trail is complete
    // then). The bridge stores `extra` inside the run's workflow evidence.
    if ( !running && m_trailSource )
    {
        QJsonArray trail = m_trailSource();
        if ( trail.size() > kOperationTrailLimit )
        {
            // Keep the MOST RECENT records; truncation is explicit.
            QJsonArray recent;
            for ( int i = trail.size() - kOperationTrailLimit; i < trail.size(); ++i )
                recent.append( trail.at( i ) );
            trail = recent;
            event.extra.insert( QStringLiteral( "operationTrailTruncated" ), true );
        }
        QJsonArray redacted;
        for ( const QJsonValue &record : trail )
            redacted.append( RunEnvironment::redactSecretKeys( record.toObject() ) );
        event.extra.insert( QStringLiteral( "operationTrail" ), redacted );
    }

    const auto recorded = m_bridge->handleExecutionEvent( event );
    if ( !recorded )
    {
        qWarning( "lab recording: %s event for %s not recorded: %s", qUtf8Printable( state ),
                  qUtf8Printable( runId ),
                  qUtf8Printable( recorded.diagnostics().first().message ) );
    }
}

} // namespace sicnu::experiment
