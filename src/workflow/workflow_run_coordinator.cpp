// src/workflow/workflow_run_coordinator.cpp — see header for the contract.
#include "workflow_run_coordinator.h"

#include "artifact_gc.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <thread>

#include "jobs/job_types.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "placeholder_grammar.h"
#include "processing/framework/task_center.h"
#include "data/artifact_store.h"
#include "data/execution_fingerprint.h"
#include "runtime/observability/trace.h"

namespace sicnu::workflow {

namespace {

/// Digest budget for resume completion identity (issue #750): outputs above
/// this size keep stat-only identity (SICNU_RESUME_DIGEST_MAX_MB, default 256;
/// 0 disables digesting entirely).
qint64 resumeDigestBudgetBytes()
{
    static const qint64 budget = []() {
        const QString raw = qEnvironmentVariable( "SICNU_RESUME_DIGEST_MAX_MB" );
        if ( raw.isEmpty() )
            return 256LL * 1024 * 1024;
        bool ok = false;
        const qint64 mb = raw.toLongLong( &ok );
        return ( ok && mb >= 0 ) ? mb * 1024 * 1024 : 256LL * 1024 * 1024;
    }();
    return budget;
}

/// Completion identity of a step's produced output: the stat stamp is the
/// minimum proof; the content digest is added when the output fits the
/// verification budget.
struct CompletionIdentity
{
    long long sizeBytes = 0;
    long long mtimeMs = 0;
    std::string digest;
};

/// Computes the identity for @a outputPath. Deliberately lock-free: the
/// digest can read up to the verification budget, and onTaskUpdated folds
/// under the coordinator mutex — hashing there would stall every tracked run
/// (review P1). The caller applies the result to the plan under the lock.
std::optional<CompletionIdentity> computeCompletionIdentity( const QString &outputPath )
{
    const QFileInfo info( outputPath );
    if ( !info.isFile() )
        return std::nullopt;
    CompletionIdentity identity;
    identity.sizeBytes = info.size();
    identity.mtimeMs = info.lastModified().toMSecsSinceEpoch();
    if ( info.size() > 0 && info.size() <= resumeDigestBudgetBytes() )
    {
        QString digestError;
        identity.digest = sicnu::data::artifactContentDigest( outputPath, &digestError ).toStdString();
        if ( !digestError.isEmpty() )
            identity.digest.clear();
    }
    return identity;
}

/// Applies a pre-computed identity to @a plan (under the coordinator mutex).
void stampCompletionIdentity( StepPlan &plan, const CompletionIdentity &identity )
{
    plan.outputSizeBytes = identity.sizeBytes;
    plan.outputMtimeMs = identity.mtimeMs;
    plan.outputDigest = identity.digest;
}

/// Current implementation identity of @p operatorId (8.0 WP-E): SHA-256 over
/// (schema text + determinism grade + fingerprint contract + platform
/// version). nullopt when the operator is unknown to the registry or its
/// schema cannot be proven — the resume gate then refuses to serve (fail
/// -closed). Registry access reads no shared mutable state beyond the
/// instance's own locks; called outside the coordinator mutex like the other
/// out-of-lock identity work.
std::optional<std::string> currentOperatorImplIdentity( const std::string &operatorId )
{
    if ( operatorId.empty() )
        return std::nullopt;
    const auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId );
    if ( !op )
        return std::nullopt;
    Json::StreamWriterBuilder schemaWriter;
    schemaWriter[ "indentation" ] = "";
    const std::string schemaText = Json::writeString( schemaWriter, op->schema() );
    const sicnu::data::ExecutionFingerprint identity =
        sicnu::data::makeImplementationIdentity( schemaText + "|grade=" + op->determinismGrade() );
    if ( !identity.isValid() )
        return std::nullopt;
    return identity.toStdString();
}

/// Re-hydrates a moved/missing step output from the content-addressed pool
/// (8.0 WP-E): when the checkpoint recorded a digest and the recorded path
/// no longer vouches for it, a verified pool object with the SAME digest is
/// staged to the declared path via temp-copy + atomic rename and its bytes
/// re-verified after the copy. Returns true only when the declared path now
/// provably holds the digested content — anything else leaves the step to
/// re-execute (fail-closed, identical to pre-8.0 behavior).
bool rehydrateMovedOutput( const StepPlan &plan )
{
    if ( plan.outputDigest.empty() || plan.outputLayerPath.empty() )
        return false;
    const auto object = sicnu::data::ExecutionResultCache::instance().pooledObjectByDigest(
        QString::fromStdString( plan.outputDigest ) );
    if ( !object || object->size <= 0 )
        return false;
    const QString destination = QString::fromStdString( plan.outputLayerPath );
    const QFileInfo destInfo( destination );
    if ( !QDir().mkpath( destInfo.absolutePath() ) )
        return false;
    const QString tmp = destination + QStringLiteral( ".%1.resume.tmp" )
#ifdef _WIN32
                            .arg( static_cast<int>( ::_getpid() ) );
#else
                            .arg( ::getpid() );
#endif
    QFile::remove( tmp );
    if ( !QFile::copy( object->poolPath, tmp ) )
    {
        QFile::remove( tmp );
        return false;
    }
    // The staged bytes must prove the digest before they may become the
    // output — never rename bytes nobody vouches for.
    QString digestError;
    const QString stagedDigest = sicnu::data::artifactContentDigest( tmp, &digestError );
    if ( !digestError.isEmpty()
         || stagedDigest != QString::fromStdString( plan.outputDigest ) )
    {
        QFile::remove( tmp );
        return false;
    }
    if ( QFile::exists( destination ) )
        QFile::remove( destination );
    if ( !QFile::rename( tmp, destination ) )
    {
        QFile::remove( tmp );
        return false;
    }
    return true;
}

/// Execution Plane 8.0 WP-I: bounded resume-decision trace. Correlation:
/// run id + step id (operator id rides the op field). One relaxed atomic
/// load when tracing is off (trace.h hot-path rule).
void traceResumeEvent( const WorkflowRun &run, const char *event, const char *status,
                       const std::string &stepId, const std::string &operatorId,
                       const std::string &detail = {} )
{
    namespace trace = sicnu::runtime::observability::trace;
    if ( !trace::Trace::enabled() )
        return;
    trace::TraceEvent record;
    record.run = run.runId();
    record.task = stepId;
    record.op = operatorId;
    record.event = event;
    record.status = status;
    record.detail = detail;
    trace::Trace::publish( record );
}

QString stepStatusForTaskStatus( sicnu::TaskStatus status )
{
    switch ( status )
    {
      case sicnu::TaskStatus::Completed:
        return QStringLiteral( "Completed" );
      case sicnu::TaskStatus::Failed:
        return QStringLiteral( "Failed" );
      case sicnu::TaskStatus::Canceled:
      case sicnu::TaskStatus::Cancelling:
        return QStringLiteral( "Canceled" );
      case sicnu::TaskStatus::Running:
      case sicnu::TaskStatus::Dispatching:
        return QStringLiteral( "Running" );
      case sicnu::TaskStatus::Queued:
      case sicnu::TaskStatus::WaitingResource:
      case sicnu::TaskStatus::Paused:
      default:
        return QStringLiteral( "Pending" );
    }
}

/// Recursively substitute placeholder refs in a JSON param tree using
/// @a resolver (string leaves only; mirrors TaskCenter's variant walker).
Json::Value substituteJsonPlaceholders( const Json::Value &value,
                                        const std::function<std::string( const PlaceholderRef & )> &resolver,
                                        bool *changed = nullptr )
{
    if ( value.isString() )
    {
        const std::string raw = value.asString();
        const std::string sub = substitutePlaceholders( raw, resolver );
        if ( sub != raw )
        {
            if ( changed )
                *changed = true;
            return Json::Value( sub );
        }
        return value;
    }
    if ( value.isArray() )
    {
        Json::Value out( Json::arrayValue );
        for ( Json::ArrayIndex i = 0; i < value.size(); ++i )
            out[i] = substituteJsonPlaceholders( value[i], resolver, changed );
        return out;
    }
    if ( value.isObject() )
    {
        Json::Value out( Json::objectValue );
        for ( const auto &name : value.getMemberNames() )
            out[name] = substituteJsonPlaceholders( value[name], resolver, changed );
        return out;
    }
    return value;
}

} // namespace

WorkflowRunCoordinator &WorkflowRunCoordinator::instance()
{
    static WorkflowRunCoordinator s_instance;
    return s_instance;
}

WorkflowRunCoordinator::WorkflowRunCoordinator() = default;

WorkflowRunCoordinator::~WorkflowRunCoordinator() = default;

void WorkflowRunCoordinator::setCheckpointDirectory( const QString &directory )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_checkpointDir = directory;
}

void WorkflowRunCoordinator::setCheckpointIoDelayForTests( int milliseconds )
{
    m_checkpointIoDelayMs.store( milliseconds < 0 ? 0 : milliseconds );
}

void WorkflowRunCoordinator::setCheckpointLoadDelayForTests( int milliseconds )
{
    m_checkpointLoadDelayMs.store( milliseconds < 0 ? 0 : milliseconds );
}

QString WorkflowRunCoordinator::checkpointDirectory() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_checkpointDir.isEmpty() ? WorkflowCheckpointManager::defaultCheckpointDirectory()
                                     : m_checkpointDir;
}

QString WorkflowRunCoordinator::checkpointDirectoryLocked() const
{
    return m_checkpointDir.isEmpty() ? WorkflowCheckpointManager::defaultCheckpointDirectory()
                                     : m_checkpointDir;
}

QString WorkflowRunCoordinator::checkpointPathLocked( const std::string &runId ) const
{
    return checkpointDirectoryLocked() + QDir::separator()
           + QStringLiteral( "checkpoint_%1.json" ).arg( QString::fromStdString( runId ) );
}

QString WorkflowRunCoordinator::checkpointPathFor( const std::string &runId ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return checkpointPathLocked( runId );
}

void WorkflowRunCoordinator::queueRunStateNotificationLocked( const WorkflowRun &run,
                                                              qint64 startedMs, qint64 finishedMs )
{
    // 9.0 M4: the seq stamp is assigned at enqueue (transition) time so a
    // consumer can reconcile enqueue order against cross-thread drain
    // interleaving.
    const quint64 notifySeq = m_nextNotifySeq++;

    qint64 effectiveStart = startedMs;
    if ( effectiveStart <= 0 )
    {
        // The run's creation stamp is the truthful start when the caller has
        // no better one (terminal/Interrupted transitions): never mirror a
        // zeroed started_ms over the value recorded at start (issue #754).
        const QDateTime createdAt =
            QDateTime::fromString( QString::fromStdString( run.createdAt() ), Qt::ISODate );
        if ( createdAt.isValid() )
            effectiveStart = createdAt.toMSecsSinceEpoch();
    }

    // Unified-trace adapter (Verification Platform 8.0): the Workflow link of
    // the chain — one exp.trace.v1 record per run-state transition, reusing
    // the SAME effectiveStart the signal carries. Runs under m_mutex by
    // design: Trace::publish is a bounded non-blocking ring push with no
    // observer callbacks, so it cannot re-enter the coordinator.
    if ( sicnu::runtime::observability::trace::Trace::enabled() )
    {
        sicnu::runtime::observability::trace::TraceEvent trace;
        trace.run = run.runId();
        trace.event = "run_state";
        trace.detail = run.workflowId() + "#seq=" + std::to_string( notifySeq );
        switch ( run.state() )
        {
        case WorkflowRunState::Completed:
            trace.status = "ok";
            break;
        case WorkflowRunState::Failed:
            trace.status = "error";
            break;
        case WorkflowRunState::Canceled:
            trace.status = "cancelled";
            break;
        case WorkflowRunState::Cancelling:
            // In-flight cancellation attempt; distinct from final Canceled.
            trace.status = "cancelling";
            break;
        case WorkflowRunState::Interrupted:
            // Terminal recovery state (crash/shutdown): its own verdict so
            // consumers never read it as ok/error/cancelled.
            trace.status = "interrupted";
            break;
        default:
            break;
        }
        if ( finishedMs > 0 && effectiveStart > 0 )
            trace.durationUs = ( finishedMs - effectiveStart ) * 1000;
        sicnu::runtime::observability::trace::Trace::publish( trace );
    }

    RunNotification notification;
    notification.seq = notifySeq;
    notification.runId = QString::fromStdString( run.runId() );
    notification.workflowId = QString::fromStdString( run.workflowId() );
    notification.state = QString::fromStdString( workflowRunStateToString( run.state() ) );
    notification.startedMs = effectiveStart;
    notification.finishedMs = finishedMs;
    m_pendingRunNotifications.push_back( std::move( notification ) );
}

void WorkflowRunCoordinator::drainRunNotifications()
{
    std::vector<RunNotification> drained;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( m_pendingRunNotifications.empty() )
            return;
        drained.swap( m_pendingRunNotifications );
    }
    // Emission happens with NO coordinator lock held (#860): a slot may now
    // safely re-enter the coordinator (runs(), runForPipeline(),
    // checkpointPathFor(), even resume) from the same thread. A re-entrant
    // drain swaps only whatever was queued after our swap — never re-emits
    // this batch. Cross-thread concurrent drains can interleave, which is
    // exactly what the per-notification seq order stamp makes detectable.
    for ( const RunNotification &notification : drained )
    {
        emit runStateChanged( notification.runId, notification.workflowId,
                              notification.state, notification.startedMs,
                              notification.finishedMs );
    }
}

WorkflowRunCoordinator::PersistRequest
WorkflowRunCoordinator::capturePersistLocked( const std::shared_ptr<WorkflowRun> &run,
                                              bool sweepAndArchive )
{
    PersistRequest request;
    if ( !run )
        return request;
    request.run = run;
    request.runId = run->runId();
    request.directory = checkpointDirectoryLocked();
    request.seq = m_nextPersistSeq++;
    m_latestPersistSeq[request.runId] = request.seq;
    request.sweepAndArchive = sweepAndArchive;
    return request;
}

void WorkflowRunCoordinator::persistRun( PersistRequest request )
{
    if ( !request.run || request.runId.empty() )
        return;

    // One-shot test delay AFTER m_mutex dropped so a concurrent
    // runForPipeline / resumeRun of a different run stays live (#931).
    const int delayMs = m_checkpointIoDelayMs.exchange( 0 );
    if ( delayMs > 0 )
        std::this_thread::sleep_for( std::chrono::milliseconds( delayMs ) );

    std::lock_guard<std::mutex> io( m_checkpointIoMutex );
    bool superseded = false;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        const auto it = m_latestPersistSeq.find( request.runId );
        superseded = it != m_latestPersistSeq.end() && request.seq < it->second;
    }
    // Best-effort persistence: a failed save never aborts the pipeline — the
    // run keeps executing; recovery then treats it as Interrupted (the state
    // on disk simply lags). Finalize still writes even when superseded so
    // ArtifactGC / archive see a terminal checkpoint.
    if ( !superseded || request.sweepAndArchive )
        m_checkpoints.saveCheckpoint( *request.run, request.directory );

    if ( request.sweepAndArchive && request.run->state() == WorkflowRunState::Completed )
    {
        ArtifactGC gc;
        gc.sweepRun( *request.run, /*retainFinalOutputs=*/true );
        WorkflowCheckpointManager::archiveCompletedRun(
            request.directory + QDir::separator()
                + QStringLiteral( "checkpoint_%1.json" ).arg( QString::fromStdString( request.runId ) ),
            request.directory );
    }
}

long WorkflowRunCoordinator::startTrackedPipelineJson( const std::string &jsonPipeline, bool autoLoad )
{
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( jsonPipeline.c_str(), jsonPipeline.c_str() + jsonPipeline.length(), &root, &errs ) )
        return -1;

    WorkflowDefinition def;
    std::string parseErr;
    if ( !workflowDefinitionFromJson( root, def, parseErr ) )
        return -1;
    return startTrackedPipeline( def, autoLoad );
}

long WorkflowRunCoordinator::startTrackedPipeline( const WorkflowDefinition &def, bool autoLoad )
{
    std::shared_ptr<WorkflowRun> run = WorkflowRun::createFromDefinition( def );
    if ( !run )
        return -1;
    run->transitionTo( WorkflowRunState::Ready );
    run->transitionTo( WorkflowRunState::Running );

    // Cross-process ownership (#727): hold the run's lock for the whole
    // execution so no other process reconciles or resumes this runId under
    // us. Acquired BEFORE TaskCenter dispatch (which starts real work).
    auto runLock = std::make_shared<WorkflowRunLock>(
        WorkflowRunLock::lockPathForRun( checkpointDirectory(), run->runId() ) );
    {
        QString heldByPid;
        const WorkflowRunLock::TryResult acquired = runLock->tryAcquire( &heldByPid );
        if ( acquired != WorkflowRunLock::TryResult::Acquired )
        {
            // A freshly generated runId colliding with a live owner is
            // practically impossible — but if it happens, the on-disk state
            // belongs to the OWNER: never write over it, just refuse.
            return -1;
        }
    }

    auto &center = TaskCenter::instance();

    // Phase J (resume hardening W1): persist the run BEFORE dispatch so a
    // crash during submitPipeline leaves a recoverable checkpoint instead of
    // real side effects with no on-disk run. The post-submission persist
    // below adds task ids and current step statuses.
    const qint64 startedMs = QDateTime::currentMSecsSinceEpoch();
    PersistRequest firstPersist;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        firstPersist = capturePersistLocked( run );
        queueRunStateNotificationLocked( *run, startedMs, 0 ); // state Running (issue #754)
    }
    persistRun( std::move( firstPersist ) );
    // Flush the Running broadcast BEFORE dispatch: a pipeline whose first
    // step fails instantly must never surface Running after its Failed
    // terminal (observers see transitions in transition order, #860).
    drainRunNotifications();

    // Seed the step plans with the pipeline's task ids so the checkpoint
    // written BEFORE dispatch already maps steps to tasks (crash during
    // submit still leaves a resumable picture).
    const long pipelineId = center.submitPipeline( def, autoLoad );
    if ( pipelineId < 0 )
    {
        run->setErrorMessage( "Pipeline contains no dispatchable operator steps" );
        run->transitionTo( WorkflowRunState::Failed );
        PersistRequest failedPersist;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            failedPersist = capturePersistLocked( run );
            queueRunStateNotificationLocked( *run, startedMs,
                                             QDateTime::currentMSecsSinceEpoch() );
        }
        persistRun( std::move( failedPersist ) );
        drainRunNotifications();
        return -1; // the local runLock shared_ptr releases the flock here
    }

    const PipelineExecutionInfo pipe = center.getPipelineInfo( pipelineId );
    for ( const auto &stepId : pipe.orderedStepIds )
    {
        const long taskId = pipe.stepToTaskId.value( stepId, -1 );
        if ( taskId < 0 )
            continue;
        if ( StepPlan *plan = run->findStepPlan( stepId ) )
        {
            plan->taskId = taskId;
            plan->status = "Pending";
            run->updateStepPlan( *plan );
        }
    }

    const std::string freshRunId = run->runId();
    PersistRequest postSubmitPersist;
    PersistRequest finalizePersist;
    bool persistFinalize = false;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( !m_connected )
        {
            // Direct connection on purpose: transitions arrive from worker
            // threads and must be persisted immediately, not gated on any
            // event loop pumping (MCP mode included).
            connect( &center, &TaskCenter::taskUpdated, this,
                     &WorkflowRunCoordinator::onTaskUpdated, Qt::DirectConnection );
            m_connected = true;
        }
        m_runsByPipeline[pipelineId] = run;
        m_pipelineByRunId[freshRunId] = pipelineId;
        m_locksByRunId[freshRunId] = std::move( runLock );

        // submitPipeline dispatched BEFORE this registration: any transition
        // in that window was missed by onTaskUpdated (review P1 — a fast first
        // step could even complete and the run would never finalize). Fold the
        // pipeline's CURRENT state in through the same per-step update path so
        // the run converges no matter what happened in between.
        const PipelineExecutionInfo pipeNow = center.getPipelineInfo( pipelineId );
        for ( const auto &stepId : pipeNow.orderedStepIds )
        {
            const long taskId = pipeNow.stepToTaskId.value( stepId, -1 );
            if ( taskId < 0 )
                continue;
            const AlgorithmTaskInfo info = center.getTaskInfo( taskId );
            if ( info.taskId != taskId )
                continue;
            if ( StepPlan *plan = run->findStepPlan( stepId ) )
            {
                plan->status = stepStatusForTaskStatus( info.status ).toStdString();
                plan->taskId = taskId;
                if ( !info.parameterMap.isEmpty() )
                    plan->resolvedParams = TaskCenter::variantMapToJsonParams( info.parameterMap );
                if ( info.status == sicnu::TaskStatus::Completed
                     && !info.outputLayerPath.isEmpty() )
                {
                    plan->outputLayerPath = info.outputLayerPath.toStdString();
                    run->setArtifact( stepId, plan->outputLayerPath );
                }
                run->updateStepPlan( *plan );
            }
        }
        postSubmitPersist = capturePersistLocked( run );

        // All steps may have gone terminal inside the missed window.
        const auto plans = run->stepPlans();
        const bool allTerminal = !plans.empty() && std::all_of( plans.begin(), plans.end(),
                                                                []( const StepPlan &p2 ) {
                                                                  return p2.status == "Completed"
                                                                         || p2.status == "Failed"
                                                                         || p2.status == "Canceled"
                                                                         || p2.status == "Skipped";
                                                                } );
        if ( allTerminal && !isTerminalRunState( run->state() ) )
        {
            finalizeRunLocked( pipelineId, *run );
            finalizePersist = capturePersistLocked(
                run, run->state() == WorkflowRunState::Completed );
            persistFinalize = true;
        }
    }
    persistRun( std::move( postSubmitPersist ) );
    if ( persistFinalize )
        persistRun( std::move( finalizePersist ) );
    // The registration block may have folded fast transitions (even a full
    // finalize) while m_mutex was held — their notifications drain here,
    // outside the lock, before the pipeline id is handed to the caller.
    drainRunNotifications();
    return pipelineId;
}

void WorkflowRunCoordinator::onTaskUpdated( const AlgorithmTaskInfo &info )
{
    if ( info.pipelineId < 0 || info.stepId.isEmpty() )
        return;

    // Reviewed P3: the identity work below (digest hashing + operator
    // schema hashing) is only consumed by a TRACKED run. Untracked
    // pipelines (GUI single tasks, untracked submissions) were folded,
    // discarded and re-paid the hashing cost on every completion.
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( m_runsByPipeline.find( info.pipelineId ) == m_runsByPipeline.end() )
            return;
    }

    // Hash OUTSIDE the fold lock (review P1): the digest may read up to the
    // verification budget, and this fold runs under m_mutex.
    std::optional<CompletionIdentity> completionIdentity;
    if ( info.status == sicnu::TaskStatus::Completed && !info.outputLayerPath.isEmpty() )
        completionIdentity = computeCompletionIdentity( info.outputLayerPath );

    // 8.0 WP-E: the producing operator's implementation identity, resolved
    // outside the fold lock for the same reason (registry + schema work).
    // A step the registry cannot resolve stays unstamped — the resume gate
    // then serves it only in the equally-unresolvable state (fail-closed).
    std::optional<std::string> operatorImplIdentity;
    if ( info.status == sicnu::TaskStatus::Completed && !info.algorithmId.isEmpty() )
        operatorImplIdentity = currentOperatorImplIdentity( info.algorithmId.toStdString() );

    // The whole fold runs under m_mutex: resumeRun swaps the mapped run
    // object under the same lock, so a transition either lands entirely
    // before the swap (visible to its merge) or entirely after (folded into
    // the swapped-in run) — never into a discarded object (#720).
    // SCOPE NOTE (#860): the brace below bounds the fold's critical section
    // so the notification drain after it runs strictly outside m_mutex.
    PersistRequest foldPersist;
    PersistRequest finalizePersist;
    bool persistFinalize = false;
    {
    std::lock_guard<std::mutex> lock( m_mutex );

    const auto it = m_runsByPipeline.find( info.pipelineId );
    if ( it == m_runsByPipeline.end() )
        return;
    std::shared_ptr<WorkflowRun> run = it->second;

    const std::string stepKey = info.stepId.toStdString();
    std::optional<StepPlan> plan = run->stepPlan( stepKey );
    if ( !plan )
        return;

    plan->status = stepStatusForTaskStatus( info.status ).toStdString();
    plan->taskId = info.taskId;
    // Provenance must describe what ACTUALLY ran (#727): fold the task's
    // live (placeholder-substituted) parameters into the persisted plan so
    // the checkpoint carries real inputs, not the definition's $step.output
    // strings. The task parameterMap converges to the substituted set before
    // dispatch (TaskCenter::applyPlaceholdersForTask).
    if ( !info.parameterMap.isEmpty() )
        plan->resolvedParams = TaskCenter::variantMapToJsonParams( info.parameterMap );
    if ( !info.startTime.isNull() )
        plan->startTime = info.startTime.toString( Qt::ISODate ).toStdString();
    if ( !info.endTime.isNull() )
        plan->endTime = info.endTime.toString( Qt::ISODate ).toStdString();
    if ( info.status == sicnu::TaskStatus::Completed )
    {
        plan->resultPayload = info.resultPayload;
        if ( !info.outputLayerPath.isEmpty() )
        {
            plan->outputLayerPath = info.outputLayerPath.toStdString();
            run->setArtifact( stepKey, plan->outputLayerPath );
            if ( completionIdentity )
                stampCompletionIdentity( *plan, *completionIdentity );
        }
        // 8.0 WP-E: bind the output to the implementation that produced it.
        if ( operatorImplIdentity )
            plan->operatorImplStamp = *operatorImplIdentity;
    }
    else if ( info.status == sicnu::TaskStatus::Failed )
    {
        plan->errorMessage = info.errorMessage.toStdString();
    }
    run->updateStepPlan( *plan );
    foldPersist = capturePersistLocked( run );

    // Terminal roll-up when every step plan reached a terminal status.
    const auto plans = run->stepPlans();
    const bool allTerminal = !plans.empty() && std::all_of( plans.begin(), plans.end(),
                                                            []( const StepPlan &p ) {
                                                              return p.status == "Completed"
                                                                     || p.status == "Failed"
                                                                     || p.status == "Canceled"
                                                                     || p.status == "Skipped";
                                                            } );
    if ( allTerminal && !isTerminalRunState( run->state() ) )
    {
        finalizeRunLocked( info.pipelineId, *run ); // exactly once per run
        finalizePersist = capturePersistLocked(
            run, run->state() == WorkflowRunState::Completed );
        persistFinalize = true;
    }
    }
    persistRun( std::move( foldPersist ) );
    if ( persistFinalize )
        persistRun( std::move( finalizePersist ) );
    // Lifecycle-safe eventing (#860): the fold's m_mutex scope ENDS above;
    // every queued notification drains strictly OUTSIDE the lock. (The
    // in-lock drain variant self-deadlocks the non-recursive mutex — caught
    // by the #860 re-entrancy regression the day it shipped.)
    drainRunNotifications();
}

void WorkflowRunCoordinator::finalizeRunLocked( long pipelineId, WorkflowRun &run )
{
    const auto plans = run.stepPlans();
    const bool anyFailed = std::any_of( plans.begin(), plans.end(),
                                        []( const StepPlan &p ) { return p.status == "Failed"; } );
    const bool anyCanceled = std::any_of( plans.begin(), plans.end(),
                                          []( const StepPlan &p ) { return p.status == "Canceled"; } );
    if ( anyFailed )
    {
        for ( const auto &p : plans )
        {
            if ( p.status == "Failed" && !p.errorMessage.empty() )
            {
                run.setErrorMessage( p.errorMessage );
                break;
            }
        }
        run.transitionTo( WorkflowRunState::Failed );
    }
    else if ( anyCanceled )
    {
        run.transitionTo( WorkflowRunState::Canceled );
    }
    else
    {
        run.transitionTo( WorkflowRunState::Completed );
    }
    queueRunStateNotificationLocked( run, 0, QDateTime::currentMSecsSinceEpoch() );
    // Checkpoint IO, ArtifactGC and archive run AFTER m_mutex drops via
    // persistRun (issue #931). The caller captures a PersistRequest with
    // sweepAndArchive when this run completed.
    // The run is terminal: whoever acquires the run lock next may resume or
    // reconcile it. (Erasing the shared_ptr releases the flock.)
    m_locksByRunId.erase( run.runId() );
    (void)pipelineId;
}

WorkflowRunCoordinator::RecoveryReport WorkflowRunCoordinator::recoverAtStartup( bool autoResume )
{
    RecoveryReport report;
    // Lock ordering contract (#727): flock(m) is acquired BEFORE m_mutex on
    // every path, never the reverse. recoverInterruptedRuns takes the run
    // locks of other processes, so it must run WITHOUT m_mutex held —
    // otherwise a concurrent resumeRun (holding a run lock, waiting for
    // m_mutex) could deadlock against this loop (holding m_mutex, waiting
    // for that run lock). The directory is snapshotted under the lock.
    const QString dir = checkpointDirectory();
    std::vector<std::shared_ptr<WorkflowRun>> recovered =
        m_checkpoints.recoverInterruptedRuns( dir );
    for ( const auto &run : recovered )
    {
        if ( !run )
            continue;
        report.interruptedRuns += 1;
        report.runIds.append( QString::fromStdString( run->runId() ) );
        if ( autoResume )
        {
            QString err;
            if ( resumeRun( run->runId(), &err ) > 0 )
                report.resumedPipelines += 1;
            else
                report.errors.append( err );
        }
    }
    return report;
}

long WorkflowRunCoordinator::resumeRun( const std::string &runId, QString *error )
{
    // resumeRun has many exit paths; the wrapper guarantees every queued
    // notification drains outside m_mutex no matter which one is taken
    // (#860). run locks (flock) may still be held at drain time: that is
    // safe — flock guards cross-process ownership and is never taken by a
    // signal slot.
    const long pipelineId = resumeRunImpl( runId, error );
    drainRunNotifications();
    return pipelineId;
}

long WorkflowRunCoordinator::resumeRunImpl( const std::string &runId, QString *error )
{
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( m_resuming.count( runId ) > 0 )
        {
            if ( error )
                *error = QStringLiteral( "Run %1 is already resuming" ).arg( QString::fromStdString( runId ) );
            return -1;
        }
        m_resuming.insert( runId );
    }
    struct ResumingGuard
    {
        WorkflowRunCoordinator *self;
        const std::string &id;
        ~ResumingGuard()
        {
            std::lock_guard<std::mutex> lock( self->m_mutex );
            self->m_resuming.erase( id );
        }
    } resumingGuard{ this, runId };

    const QString path = checkpointPathFor( runId );
    if ( !QFile::exists( path ) )
    {
        if ( error )
            *error = QStringLiteral( "No checkpoint for run %1" ).arg( QString::fromStdString( runId ) );
        return -1;
    }

    // Cross-process ownership (#727): acquire the run's lock BEFORE looking
    // at the checkpoint. A run currently executing in another process holds
    // its lock until it finalizes, so a second `--resume` of the same runId
    // is refused here instead of double-executing the workflow; and once WE
    // hold the lock, an active on-disk state is stale by construction (the
    // previous owner died) and can be reconciled inline.
    auto runLock = std::make_shared<WorkflowRunLock>(
        WorkflowRunLock::lockPathForRun( checkpointDirectory(), runId ) );
    {
        QString heldByPid;
        const WorkflowRunLock::TryResult acquired = runLock->tryAcquire( &heldByPid );
        if ( acquired != WorkflowRunLock::TryResult::Acquired )
        {
            if ( acquired == WorkflowRunLock::TryResult::HeldByLiveOwner )
            {
                if ( error )
                    *error = QStringLiteral( "Run %1 is owned by a live process (pid %2) — resume refused" )
                               .arg( QString::fromStdString( runId ),
                                     heldByPid.isEmpty() ? QStringLiteral( "?" ) : heldByPid );
            }
            else if ( error )
            {
                *error = QStringLiteral( "Cannot lock run %1 for resumption" )
                           .arg( QString::fromStdString( runId ) );
            }
            return -1;
        }
    }
    // File read + JSON parse must NOT hold m_mutex (issue #944 / leftover of
    // #931). Path was taken from the caller; install the lock map entry only
    // after a successful load, and refuse if that runId is already tracked.
    QString loadErr;
    // One-shot test delay so a concurrent runs()/runForPipeline of another
    // run can prove it is not waiting on this resume's FS (#944).
    {
        const int delayMs = m_checkpointLoadDelayMs.exchange( 0 );
        if ( delayMs > 0 )
            std::this_thread::sleep_for( std::chrono::milliseconds( delayMs ) );
    }
    std::shared_ptr<WorkflowRun> run = m_checkpoints.loadCheckpoint( path, &loadErr );
    if ( !run )
    {
        if ( error )
            *error = QStringLiteral( "Checkpoint rejected for run %1: %2" )
                       .arg( QString::fromStdString( runId ), loadErr );
        return -1;
    }
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( m_pipelineByRunId.count( runId ) > 0 )
        {
            if ( error )
                *error = QStringLiteral( "Run %1 is already tracked" )
                           .arg( QString::fromStdString( runId ) );
            return -1;
        }
        m_locksByRunId[runId] = runLock;
    }

    // We hold the run lock: an active state means the previous owner is gone
    // (crash) — reconcile it right here instead of requiring a prior
    // recoverAtStartup pass (the MCP resume_workflow surface has none).
    if ( WorkflowCheckpointManager::reconcileToInterrupted( *run ) )
    {
        PersistRequest reconcilePersist;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            reconcilePersist = capturePersistLocked( run );
            queueRunStateNotificationLocked( *run, 0, QDateTime::currentMSecsSinceEpoch() ); // Interrupted
        }
        persistRun( std::move( reconcilePersist ) );
    }
    if ( run->state() != WorkflowRunState::Interrupted && run->state() != WorkflowRunState::Failed
         && run->state() != WorkflowRunState::Canceled )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_locksByRunId.erase( runId ); // releases the run lock
        if ( error )
            *error = QStringLiteral( "Run %1 is %2 — only interrupted/failed/canceled runs resume" )
                       .arg( QString::fromStdString( runId ),
                             QString::fromStdString( workflowRunStateToString( run->state() ) ) );
        return -1;
    }

    // Steps already completed whose outputs still exist keep BOTH their
    // result payload and canonical output path: port-aware resolution needs
    // the payload's named ports, not just the raster path (#727 RC2).
    struct CompletedResult
    {
        Json::Value payload;
        std::string outputPath;
    };
    std::map<std::string, CompletedResult> completedResults;
    for ( const StepPlan &planRef : run->stepPlans() )
    {
        StepPlan plan = planRef; // mutable copy: a re-hydrated identity converges below
        if ( plan.status != "Completed" || plan.outputLayerPath.empty() )
            continue;
        // 8.0 WP-E operator-implementation gate: the step may only be served
        // when the operator implementation that would run NOW is provably
        // the one that produced the recorded output.
        //   stamp recorded + identity resolvable  ⇒ equal serves, changed
        //                                          re-executes;
        //   stamp recorded + operator unresolvable ⇒ re-executes (fail-closed);
        //   no stamp (legacy checkpoint) + resolvable ⇒ re-executes
        //       (the completion never proved which implementation ran);
        //   no stamp + unresolvable ⇒ serve (both sides unknown-implementation;
        //       nothing provable changed — this is the pre-8.0 behavior).
        const auto currentIdentity = currentOperatorImplIdentity( plan.operatorId );
        if ( !plan.operatorImplStamp.empty() )
        {
            if ( !currentIdentity || *currentIdentity != plan.operatorImplStamp )
            {
                traceResumeEvent( *run, "resume", "operator_changed", plan.stepId,
                                  plan.operatorId );
                continue;
            }
        }
        else if ( currentIdentity )
        {
            traceResumeEvent( *run, "resume", "legacy_no_stamp", plan.stepId,
                              plan.operatorId );
            continue;
        }

        // Phase J (W2): a mere existence check once served a truncated
        // crash-era intermediate to downstream steps. Require a real,
        // non-empty file — a rewrite mid-crash fails this and is re-executed.
        QFileInfo outputInfo( QString::fromStdString( plan.outputLayerPath ) );
        if ( !outputInfo.isFile() || outputInfo.size() <= 0 )
        {
            // 8.0 WP-E moved-output recovery: the recorded path lost the
            // bytes, but the checkpoint recorded a digest — re-hydrate from
            // the content-addressed pool when a verified object with that
            // digest exists. Anything else re-executes (pre-8.0 behavior).
            if ( !rehydrateMovedOutput( plan ) )
            {
                traceResumeEvent( *run, "resume", "output_lost", plan.stepId,
                                  plan.operatorId );
                continue;
            }
            traceResumeEvent( *run, "resume", "rehydrated", plan.stepId,
                              plan.operatorId, plan.outputDigest );
            outputInfo = QFileInfo( QString::fromStdString( plan.outputLayerPath ) );
            if ( !outputInfo.isFile() || outputInfo.size() <= 0 )
                continue;
            // Converge the persisted stat identity to the restored file so
            // later resumes take the normal stat path (the digest re-check
            // below remains the actual proof — the restored bytes were
            // verified before the rename and are verified again there).
            plan.outputSizeBytes = outputInfo.size();
            plan.outputMtimeMs = outputInfo.lastModified().toMSecsSinceEpoch();
            if ( StepPlan *persisted = run->findStepPlan( plan.stepId ) )
            {
                persisted->outputSizeBytes = plan.outputSizeBytes;
                persisted->outputMtimeMs = plan.outputMtimeMs;
                run->updateStepPlan( *persisted );
            }
        }
        // Completion identity gate (issue #750): the checkpoint must prove
        // the bytes at the recorded path are the ones the step completed
        // with. Stat identity (size + mtime) is checked when recorded; the
        // content digest is additionally verified when the checkpoint
        // carries one. A legacy checkpoint without any identity — or any
        // mismatch — re-executes the step: resume stays semantically
        // equivalent to fresh execution (#731), never serving foreign bytes.
        if ( plan.outputSizeBytes <= 0 )
            continue;
        if ( plan.outputSizeBytes != outputInfo.size()
             || plan.outputMtimeMs != outputInfo.lastModified().toMSecsSinceEpoch() )
            continue;
        if ( !plan.outputDigest.empty() )
        {
            // A recorded digest MUST re-verify: an unreadable file, a digest
            // error, or any budget change between stamp and resume demotes to
            // re-execution — never a stat-only pass on bytes a digest was
            // supposed to vouch for (fail-closed, issue #750).
            QString digestError;
            const QString actualDigest =
                sicnu::data::artifactContentDigest( QString::fromStdString( plan.outputLayerPath ),
                                                    &digestError );
            if ( !digestError.isEmpty() || actualDigest.toStdString() != plan.outputDigest )
                continue;
        }
        traceResumeEvent( *run, "resume", "served", plan.stepId, plan.operatorId );
        completedResults[plan.stepId] = { plan.resultPayload, plan.outputLayerPath };
    }

    WorkflowDefinition def = run->definition();

    // Pass 1 — classify EVERY step before any edge is filtered: the
    // resubmitted set must be complete up front, otherwise edge retention
    // depends on the definition's declaration order (which is explicitly
    // arbitrary — #727 RC1).
    std::set<std::string> resubmittedIds;
    for ( const auto &step : def.steps )
    {
        if ( completedResults.count( step.id ) == 0 )
            resubmittedIds.insert( step.id );
    }

    // The ONE resolver shared with fresh dispatch (TaskCenter): named ports
    // resolve from the result payload, only genuine output refs fall back to
    // the canonical output path, and nothing resolves to a wrong port's data.
    auto resolver = [&]( const PlaceholderRef &ref ) -> std::string {
        const auto it = completedResults.find( ref.stepId );
        if ( it == completedResults.end() )
            return ref.rawRef; // live parent ref: TaskCenter resolves at dispatch
        const std::string resolved =
            resolvePlaceholderPort( it->second.payload, it->second.outputPath, ref.portName );
        return resolved.empty() ? ref.rawRef : resolved;
    };

    WorkflowDefinition remaining;
    remaining.id = def.id + "_resume";
    remaining.title = def.title;
    for ( const auto &step : def.steps )
    {
        if ( completedResults.count( step.id ) > 0 )
        {
            // Served from its persisted output — never re-executed. Its own
            // params are substituted with the SAME shared resolver and
            // persisted into the run: the step's provenance record then
            // describes the inputs it actually consumed, instead of silently
            // dropping every $step.output reference (#727 RC4).
            bool changed = false;
            const Json::Value substituted =
                substituteJsonPlaceholders( step.params, resolver, &changed );
            if ( changed )
            {
                if ( StepPlan *plan = run->findStepPlan( step.id ) )
                {
                    plan->resolvedParams = substituted;
                    run->updateStepPlan( *plan );
                }
            }
            continue;
        }

        StepDef resumed = step;
        bool changed = false;
        resumed.params = substituteJsonPlaceholders( step.params, resolver, &changed );
        // Pass 2 — edges are filtered against the COMPLETE resubmitted set
        // computed above, never against `remaining.steps` as built so far:
        // a legal `steps: [child, parent]` declaration keeps parent→child,
        // making resume's DAG identical to fresh execution's (#727 RC1).
        StepDef withLiveEdges = resumed;
        withLiveEdges.inputs.clear();
        for ( const auto &conn : resumed.inputs )
        {
            if ( resubmittedIds.count( conn.fromStepId ) > 0 )
                withLiveEdges.inputs.push_back( conn );
        }
        remaining.steps.push_back( withLiveEdges );
    }
    if ( remaining.steps.empty() )
    {
        if ( error )
            *error = QStringLiteral( "Run %1 has nothing left to resume" )
                       .arg( QString::fromStdString( runId ) );
        std::lock_guard<std::mutex> lock( m_mutex );
        m_locksByRunId.erase( runId ); // releases the run lock
        return -1;
    }

    if ( !run->transitionTo( WorkflowRunState::Running ) )
        run->forceSetState( WorkflowRunState::Running );
    const long pipelineId = startTrackedPipeline( remaining, /*autoLoad=*/true );
    if ( pipelineId < 0 )
    {
        run->forceSetState( WorkflowRunState::Failed );
        run->setErrorMessage( "Resume submission failed" );
        PersistRequest failedResumePersist;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_locksByRunId.erase( runId ); // releases the run lock
            failedResumePersist = capturePersistLocked( run );
        }
        persistRun( std::move( failedResumePersist ) );
        if ( error )
            *error = QStringLiteral( "Resume submission failed for run %1" )
                       .arg( QString::fromStdString( runId ) );
        return -1;
    }
    // startTrackedPipeline created a FRESH run for the resume submission;
    // swap in the ORIGINAL interrupted run so one runId carries the whole
    // lineage and a second interruption resumes from the union of both
    // passes (its Completed step plans + artifacts are already inside).
    std::vector<PersistRequest> swapPersists;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        const auto it = m_runsByPipeline.find( pipelineId );
        if ( it != m_runsByPipeline.end() )
        {
            const auto &center = TaskCenter::instance();
            const PipelineExecutionInfo pipeNow = center.getPipelineInfo( pipelineId );
            // Keep the fresh submission's live task ids and any completed outcomes on the original plans.
            for ( const auto &fresh : it->second->stepPlans() )
            {
                if ( StepPlan *plan = run->findStepPlan( fresh.stepId ) )
                {
                    plan->taskId = fresh.taskId;
                    // The fresh submission's plans carry the SUBSTITUTED
                    // parameters (createFromDefinition seeds resolvedParams
                    // from the resume definition's already-substituted step
                    // params): persist them so the checkpoint's provenance
                    // describes what actually ran, not raw $step.output
                    // strings (#727 RC4).
                    plan->resolvedParams = fresh.resolvedParams;
                    if ( fresh.status == "Completed" || fresh.status == "Failed"
                         || fresh.status == "Canceled" || fresh.status == "Skipped" )
                    {
                        plan->status = fresh.status;
                        plan->outputLayerPath = fresh.outputLayerPath;
                        plan->resultPayload = fresh.resultPayload;
                        plan->errorMessage = fresh.errorMessage;
                        plan->outputSizeBytes = fresh.outputSizeBytes;
                        plan->outputMtimeMs = fresh.outputMtimeMs;
                        plan->outputDigest = fresh.outputDigest;
                        // Reviewed P2 fix: the implementation identity must
                        // follow the output through the swap — a stale stamp
                        // here would re-execute a provably-valid step on the
                        // next resume (and worse, record new-implementation
                        // bytes under the old implementation's stamp).
                        plan->operatorImplStamp = fresh.operatorImplStamp;
                        if ( !plan->outputLayerPath.empty() )
                            run->setArtifact( fresh.stepId, plan->outputLayerPath );
                    }
                    else
                    {
                        const long tid = pipeNow.stepToTaskId.value( fresh.stepId, fresh.taskId );
                        const AlgorithmTaskInfo info = tid >= 0 ? center.getTaskInfo( tid ) : AlgorithmTaskInfo{};
                        if ( info.taskId == tid && tid >= 0 )
                        {
                            plan->status = stepStatusForTaskStatus( info.status ).toStdString();
                            if ( !info.parameterMap.isEmpty() )
                                plan->resolvedParams =
                                    TaskCenter::variantMapToJsonParams( info.parameterMap );
                            if ( info.status == sicnu::TaskStatus::Completed && !info.outputLayerPath.isEmpty() )
                            {
                                plan->outputLayerPath = info.outputLayerPath.toStdString();
                                run->setArtifact( fresh.stepId, plan->outputLayerPath );
                                // Missed-window fold (rare path): this runs
                                // inside the swap's m_mutex, so only the cheap
                                // stat stamp lands here — hashing would stall
                                // all coordinator traffic. Digest-bearing
                                // identity is stamped by the normal onTaskUpdated
                                // fold; a stat-only checkpoint still fails the
                                // resume gate on any byte drift it can see.
                                const QFileInfo stamped( info.outputLayerPath );
                                if ( stamped.isFile() )
                                {
                                    plan->outputSizeBytes = stamped.size();
                                    plan->outputMtimeMs = stamped.lastModified().toMSecsSinceEpoch();
                                }
                            }
                        }
                        else if ( plan->status != "Completed" )
                        {
                            plan->status = "Pending";
                        }
                    }
                    run->updateStepPlan( *plan );
                    if ( fresh.status == "Completed" && !fresh.outputLayerPath.empty() )
                        run->setArtifact( fresh.stepId, fresh.outputLayerPath );
                }
            }
            // Ghost-run closure (#876): the fresh submission already
            // broadcast Running under THIS temporary runId, so dropping it
            // silently would strand a non-terminal run forever in every
            // observer (WorkspaceService's runs index included). The ghost
            // reaches a terminal state here — Canceled with an explanatory
            // error — and its terminal notification is queued through the
            // regular channel before the mapping disappears.
            const std::string ghostRunId = it->second->runId();
            std::shared_ptr<WorkflowRun> ghost = it->second;
            if ( !isTerminalRunState( ghost->state() ) )
            {
                ghost->forceSetState( WorkflowRunState::Canceled );
                ghost->setErrorMessage(
                    QStringLiteral( "Superseded by resume of run %1 (temporary internal run)" )
                        .arg( QString::fromStdString( runId ) )
                        .toStdString() );
                // Already inside the swap's m_mutex scope: capture + queue
                // only — persist runs after the lock drops; the public
                // resumeRun wrapper drains the queue outside the lock.
                swapPersists.push_back( capturePersistLocked( ghost ) );
                queueRunStateNotificationLocked( *ghost, 0,
                                                 QDateTime::currentMSecsSinceEpoch() );
            }
            m_pipelineByRunId.erase( ghostRunId );
            // The fresh submission persisted a checkpoint under ITS runId
            // before the swap; recovery would resurrect it as a ghost
            // Interrupted run duplicating this resume (review P1). Its lock
            // is released with it — the ORIGINAL run's lock (held by this
            // resuming process) remains the ownership handle until finalize.
            QFile::remove( checkpointPathLocked( ghostRunId ) );
            m_locksByRunId.erase( ghostRunId );
            it->second = run;
            m_pipelineByRunId[run->runId()] = pipelineId;
            swapPersists.push_back( capturePersistLocked( run ) );

            // Check if all steps already finished before the swap landed
            const auto plans = run->stepPlans();
            const bool allTerminal = !plans.empty() && std::all_of( plans.begin(), plans.end(),
                                                                    []( const StepPlan &p2 ) {
                                                                      return p2.status == "Completed"
                                                                             || p2.status == "Failed"
                                                                             || p2.status == "Canceled"
                                                                             || p2.status == "Skipped";
                                                                    } );
            if ( allTerminal && !isTerminalRunState( run->state() ) )
            {
                finalizeRunLocked( pipelineId, *run );
                swapPersists.push_back( capturePersistLocked(
                    run, run->state() == WorkflowRunState::Completed ) );
            }
        }
    }
    for ( PersistRequest &persist : swapPersists )
        persistRun( std::move( persist ) );
    return pipelineId;
}

bool WorkflowRunCoordinator::cancelRun( long pipelineId )
{
    std::shared_ptr<WorkflowRun> run;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        const auto it = m_runsByPipeline.find( pipelineId );
        if ( it == m_runsByPipeline.end() )
            return false;
        run = it->second;
    }
    run->transitionTo( WorkflowRunState::Cancelling );
    PersistRequest cancelPersist;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        cancelPersist = capturePersistLocked( run );
    }
    persistRun( std::move( cancelPersist ) );
    return TaskCenter::instance().cancelPipeline( pipelineId );
}

std::shared_ptr<WorkflowRun> WorkflowRunCoordinator::runForPipeline( long pipelineId ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    const auto it = m_runsByPipeline.find( pipelineId );
    if ( it == m_runsByPipeline.end() )
        return nullptr;
    return it->second;
}

long WorkflowRunCoordinator::pipelineIdForRun( const std::string &runId ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    const auto it = m_pipelineByRunId.find( runId );
    return it != m_pipelineByRunId.end() ? it->second : -1;
}

std::vector<std::shared_ptr<WorkflowRun>> WorkflowRunCoordinator::runs() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    std::vector<std::shared_ptr<WorkflowRun>> out;
    out.reserve( m_runsByPipeline.size() );
    for ( const auto &kv : m_runsByPipeline )
        out.push_back( kv.second );
    return out;
}

QString WorkflowRunCoordinator::explainRun( long pipelineId ) const
{
    // 9.0 M7: run-level evidence dump. Copied under m_mutex, formatted
    // outside it; the run aggregate's own locking covers its step plans.
    struct RunEvidence
    {
        std::shared_ptr<WorkflowRun> run;
        bool resuming = false;
        bool runLockHeld = false;
    };
    RunEvidence evidence;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        const auto it = m_runsByPipeline.find( pipelineId );
        if ( it == m_runsByPipeline.end() )
            return QStringLiteral( "WorkflowRunCoordinator: no run tracked for pipeline %1\n" )
                .arg( pipelineId );
        evidence.run = it->second;
        evidence.resuming = m_resuming.count( evidence.run->runId() ) > 0;
        evidence.runLockHeld = m_locksByRunId.count( evidence.run->runId() ) > 0;
    }

    QString out;
    out += QStringLiteral( "WorkflowRunCoordinator run dump\n" );
    out += QStringLiteral( "  pipelineId: %1\n" ).arg( pipelineId );
    out += QStringLiteral( "  runId: %1\n" ).arg( QString::fromStdString( evidence.run->runId() ) );
    out += QStringLiteral( "  workflowId: %1\n" ).arg( QString::fromStdString( evidence.run->workflowId() ) );
    out += QStringLiteral( "  state: %1\n" )
               .arg( QString::fromStdString( workflowRunStateToString( evidence.run->state() ) ) );
    out += QStringLiteral( "  resuming: %1  runLockHeld: %2\n" )
               .arg( evidence.resuming ? QStringLiteral( "yes" ) : QStringLiteral( "no" ) )
               .arg( evidence.runLockHeld ? QStringLiteral( "yes" ) : QStringLiteral( "no" ) );
    const auto plans = evidence.run->stepPlans();
    out += QStringLiteral( "  steps: %1\n" ).arg( plans.size() );
    for ( const StepPlan &plan : plans )
    {
        out += QStringLiteral( "    step %1: status=%2 taskId=%3 outputDigest=%4\n" )
                   .arg( QString::fromStdString( plan.stepId ),
                         QString::fromStdString( plan.status ) )
                   .arg( plan.taskId )
                   .arg( plan.outputDigest.empty() ? QStringLiteral( "-" )
                                                   : QString::fromStdString( plan.outputDigest ).left( 12 ) );
    }
    return out;
}

QString WorkflowRunCoordinator::explainDump() const
{
    size_t liveRuns = 0;
    size_t terminalRuns = 0;
    size_t locksHeld = 0;
    size_t pendingNotifications = 0;
    std::vector<std::string> resumingRunIds;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        for ( const auto &kv : m_runsByPipeline )
        {
            if ( isTerminalRunState( kv.second->state() ) )
                ++terminalRuns;
            else
                ++liveRuns;
        }
        locksHeld = m_locksByRunId.size();
        pendingNotifications = m_pendingRunNotifications.size();
        resumingRunIds.assign( m_resuming.begin(), m_resuming.end() );
    }
    QString out;
    out += QStringLiteral( "WorkflowRunCoordinator explain dump\n" );
    out += QStringLiteral( "  runs: %1 live, %2 terminal\n" ).arg( liveRuns ).arg( terminalRuns );
    out += QStringLiteral( "  runLocks held: %1\n" ).arg( locksHeld );
    out += QStringLiteral( "  pendingNotifications: %1\n" ).arg( pendingNotifications );
    out += QStringLiteral( "  resuming: %1\n" ).arg( resumingRunIds.size() );
    for ( const std::string &id : resumingRunIds )
        out += QStringLiteral( "    %1\n" ).arg( QString::fromStdString( id ) );
    return out;
}

} // namespace sicnu::workflow
