// workflow_experiment_adapter.cpp — see workflow_experiment_adapter.h.
#include "workflow_experiment_adapter.h"

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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtLogging>

#include <json/json.h>

namespace sicnu::experiment
{

namespace
{

/// jsoncpp → QJson conversion for definition snapshots. Definitions are
/// caller-authored documents (bounded in practice); nesting is capped so a
/// pathological definition cannot balloon the record.
QJsonValue jsonCppToQJson( const Json::Value &value, int depth = 0 )
{
    if ( depth > 16 )
        return QJsonValue( QStringLiteral( "…" ) );
    switch ( value.type() )
    {
        case Json::nullValue:
            return QJsonValue( QJsonValue::Null );
        case Json::intValue:
            return QJsonValue( value.asInt64() );
        case Json::uintValue:
            return QJsonValue( qint64( value.asUInt64() ) );
        case Json::realValue:
            return QJsonValue( value.asDouble() );
        case Json::stringValue:
            return QJsonValue( QString::fromStdString( value.asString() ) );
        case Json::booleanValue:
            return QJsonValue( value.asBool() );
        case Json::arrayValue:
        {
            QJsonArray array;
            const Json::ArrayIndex count = qMin<Json::ArrayIndex>( value.size(), 512 );
            for ( Json::ArrayIndex i = 0; i < count; ++i )
                array.append( jsonCppToQJson( value[i], depth + 1 ) );
            return array;
        }
        case Json::objectValue:
        {
            QJsonObject object;
            int written = 0;
            for ( const std::string &key : value.getMemberNames() )
            {
                if ( written++ >= 512 )
                    break;
                object.insert( QString::fromStdString( key ),
                               jsonCppToQJson( value[key], depth + 1 ) );
            }
            return object;
        }
    }
    return QJsonValue( QJsonValue::Null );
}

QJsonObject stepSummary( const workflow::StepPlan &plan )
{
    QJsonObject summary;
    summary.insert( QStringLiteral( "id" ), QString::fromStdString( plan.stepId ) );
    summary.insert( QStringLiteral( "operator" ), QString::fromStdString( plan.operatorId ) );
    summary.insert( QStringLiteral( "status" ), QString::fromStdString( plan.status ) );
    if ( !plan.errorMessage.empty() )
        summary.insert( QStringLiteral( "error" ), QString::fromStdString( plan.errorMessage ) );
    if ( !plan.outputLayerPath.empty() )
    {
        QJsonObject output;
        output.insert( QStringLiteral( "path" ), QString::fromStdString( plan.outputLayerPath ) );
        if ( plan.outputSizeBytes > 0 )
            output.insert( QStringLiteral( "size" ), plan.outputSizeBytes );
        if ( !plan.outputDigest.empty() )
            output.insert( QStringLiteral( "digest" ),
                           QString::fromStdString( plan.outputDigest ) );
        summary.insert( QStringLiteral( "output" ), output );
    }
    return summary;
}

QString checkpointPathFor( const QString &directory, const QString &runId )
{
    return directory + QDir::separator() + QStringLiteral( "checkpoint_%1.json" ).arg( runId );
}

} // namespace

ExecutionEvent workflowRunToExecutionEvent( const workflow::WorkflowRun &run,
                                            const QString &state, qint64 startedMs,
                                            qint64 finishedMs )
{
    ExecutionEvent event;
    event.executionRef = QString::fromStdString( run.runId() );
    event.workflowId = QString::fromStdString( run.workflowId() );
    if ( event.workflowId.isEmpty() )
        event.workflowId = QString::fromStdString( run.definition().id );
    event.state = state;
    event.startedMs = startedMs;
    event.finishedMs = finishedMs;
    event.errorMessage = QString::fromStdString( run.errorMessage() );
    event.definition = jsonCppToQJson( workflowDefinitionToJson( run.definition() ) )
                           .toObject();

    const auto plans = run.stepPlans();
    for ( const auto &plan : plans )
        event.steps.append( stepSummary( plan ) );

    const std::map<std::string, std::string> artifacts = run.artifacts();
    for ( const auto &kv : artifacts )
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "path" ), QString::fromStdString( kv.second ) );
        // Enrich with completion identity when the step plan carries it.
        if ( const auto plan = run.stepPlan( kv.first ) )
        {
            if ( plan->outputSizeBytes > 0 )
                entry.insert( QStringLiteral( "size" ), plan->outputSizeBytes );
            if ( !plan->outputDigest.empty() )
                entry.insert( QStringLiteral( "digest" ),
                              QString::fromStdString( plan->outputDigest ) );
        }
        event.artifacts.insert( QString::fromStdString( kv.first ), entry );
    }
    return event;
}

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

bool WorkflowExperimentMonitor::enable( const QString &experimentDbPath,
                                        const QString &experimentId,
                                        const QString &experimentName,
                                        const QString &objective,
                                        const QString &datasetDbPath, QString *error )
{
    if ( experimentDbPath.isEmpty() || experimentId.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "experiment db path and experiment id are required" );
        return false;
    }
    if ( !m_store.isOpen() && !m_store.open( experimentDbPath, error ) )
        return false;

    if ( !m_bridge )
        m_bridge = std::make_unique<ExperimentRunBridge>( m_store );

    if ( !datasetDbPath.isEmpty() )
    {
        // The dataset store rides only when the caller pinned its path; a
        // missing/unopenable dataset db disables pin VERIFICATION, not
        // recording (recorded runs keep the raw pin ids, honestly unpinned
        // fingerprints).
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
        if ( probe.tryAcquire( &heldByPid ) != workflow::WorkflowRunLock::TryResult::Acquired )
            return ExecutionEvidence{ executionRef, QString(), false, /*liveOwner=*/true };
        // The probe now owns the lock — release immediately so a concurrent
        // recovery/resume in another process is not blocked by a stale probe.
        probe.release();

        QString path = checkpointPathFor( directory, executionRef );
        if ( !QFile::exists( path ) )
        {
            // Completed runs are archived under <dir>/history/.
            path = directory + QDir::separator() + QStringLiteral( "history" )
                   + QDir::separator() + QStringLiteral( "checkpoint_%1.json" ).arg( executionRef );
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
    if ( !bridgeExecutionStates().contains( state ) )
        return; // transitional workflow state: no experiment meaning

    ExecutionEvent event;
    event.executionRef = runId;
    event.workflowId = workflowId;
    event.state = state;
    event.startedMs = startedMs;
    event.finishedMs = finishedMs;

    // Enrich from the authoritative run aggregate when it is still tracked
    // (terminal runs stay registered, so this answers for the whole story).
    if ( const long pipelineId = m_coordinator.pipelineIdForRun( runId.toStdString() );
         pipelineId > 0 )
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
