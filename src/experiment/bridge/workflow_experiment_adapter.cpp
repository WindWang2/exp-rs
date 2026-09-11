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

namespace
{

/// jsoncpp → QJson conversion for definition snapshots. Definitions are
/// caller-authored documents (bounded in practice); nesting is capped so a
/// pathological definition cannot balloon the record. Truncation is marked
/// with an explicit "…truncated" entry, never silent.
QJsonValue jsonCppToQJson( const Json::Value &value, int depth = 0 )
{
    if ( depth > 16 )
        return QJsonValue( QStringLiteral( "…truncated" ) );
    switch ( value.type() )
    {
        case Json::nullValue:
            return QJsonValue( QJsonValue::Null );
        case Json::intValue:
            // QJsonValue stores numbers as double (Qt JSON contract); a
            // definition carrying >2^53 magnitudes is out of contract.
            return QJsonValue( static_cast<double>( value.asInt64() ) );
        case Json::uintValue:
            return QJsonValue( static_cast<double>( value.asUInt64() ) );
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
            if ( count < value.size() )
                array.append( QStringLiteral( "…truncated" ) );
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
                {
                    object.insert( QStringLiteral( "…truncated" ), true );
                    break;
                }
                object.insert( QString::fromStdString( key ),
                               jsonCppToQJson( value[key], depth + 1 ) );
            }
            return object;
        }
    }
    return QJsonValue( QJsonValue::Null );
}

QJsonObject stepSummary( const Json::Value &plan )
{
    QJsonObject summary;
    summary.insert( QStringLiteral( "id" ),
                    QString::fromStdString( plan["stepId"].asString() ) );
    summary.insert( QStringLiteral( "operator" ),
                    QString::fromStdString( plan["operatorId"].asString() ) );
    summary.insert( QStringLiteral( "status" ),
                    QString::fromStdString( plan["status"].asString() ) );
    const Json::Value &error = plan["errorMessage"];
    if ( error.isString() && !error.asString().empty() )
        summary.insert( QStringLiteral( "error" ),
                        QString::fromStdString( error.asString() ) );
    const Json::Value &outputPath = plan["outputLayerPath"];
    if ( outputPath.isString() && !outputPath.asString().empty() )
    {
        QJsonObject output;
        output.insert( QStringLiteral( "path" ),
                       QString::fromStdString( outputPath.asString() ) );
        const Json::Value &size = plan["outputSizeBytes"];
        if ( size.isInt64() && size.asInt64() > 0 )
            output.insert( QStringLiteral( "size" ),
                           static_cast<double>( size.asInt64() ) );
        const Json::Value &digest = plan["outputDigest"];
        if ( digest.isString() && !digest.asString().empty() )
            output.insert( QStringLiteral( "digest" ),
                           QString::fromStdString( digest.asString() ) );
        summary.insert( QStringLiteral( "output" ), output );
    }
    return summary;
}

} // namespace

ExecutionEvent workflowRunToExecutionEvent( const workflow::WorkflowRun &run,
                                            const QString &state, qint64 startedMs,
                                            qint64 finishedMs )
{
    // ONE locked snapshot: the coordinator's fold thread (TaskCenter
    // callbacks) may mutate the aggregate concurrently, and unlocked
    // accessors like definition() deliberately escape the run's mutex —
    // reading them here raced the fold and corrupted the JSON heap. Every
    // value below comes from the single internally-locked serialization.
    const Json::Value snapshot = run.toJson();

    ExecutionEvent event;
    event.executionRef = QString::fromStdString( snapshot["runId"].asString() );
    event.workflowId = QString::fromStdString( snapshot["workflowId"].asString() );
    if ( event.workflowId.isEmpty() )
        event.workflowId =
            QString::fromStdString( snapshot["definition"]["id"].asString() );
    event.state = state;
    event.startedMs = startedMs;
    event.finishedMs = finishedMs;
    const Json::Value &errorMessage = snapshot["errorMessage"];
    if ( errorMessage.isString() )
        event.errorMessage = QString::fromStdString( errorMessage.asString() );
    event.definition = jsonCppToQJson( snapshot["definition"] ).toObject();

    const Json::Value &plans = snapshot["stepPlans"];
    std::map<std::string, std::pair<double, QString>> identityByStep; // completion identity
    for ( const auto &plan : plans )
    {
        event.steps.append( stepSummary( plan ) );
        const Json::Value &digest = plan["outputDigest"];
        const Json::Value &size = plan["outputSizeBytes"];
        if ( digest.isString() && !digest.asString().empty() )
            identityByStep[plan["stepId"].asString()] = std::make_pair(
                size.isInt64() && size.asInt64() > 0 ? static_cast<double>( size.asInt64() )
                                                     : -1.0,
                QString::fromStdString( digest.asString() ) );
    }

    const Json::Value &artifacts = snapshot["artifacts"];
    for ( const std::string &stepId : artifacts.getMemberNames() )
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "path" ),
                      QString::fromStdString( artifacts[stepId].asString() ) );
        const auto identity = identityByStep.find( stepId );
        if ( identity != identityByStep.end() )
        {
            if ( identity->second.first > 0 )
                entry.insert( QStringLiteral( "size" ), identity->second.first );
            if ( !identity->second.second.isEmpty() )
                entry.insert( QStringLiteral( "digest" ), identity->second.second );
        }
        event.artifacts.insert( QString::fromStdString( stepId ), entry );
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
