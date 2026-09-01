// src/workflow/workflow_run_coordinator.cpp — see header for the contract.
#include "workflow_run_coordinator.h"

#include "artifact_gc.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include "jobs/job_types.h"
#include "placeholder_grammar.h"
#include "processing/framework/task_center.h"

namespace sicnu::workflow {

namespace {

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

void WorkflowRunCoordinator::persistRunLocked( WorkflowRun &run )
{
    // Best-effort persistence: a failed save never aborts the pipeline — the
    // run keeps executing; recovery then treats it as Interrupted (the state
    // on disk simply lags).
    m_checkpoints.saveCheckpoint( run, checkpointDirectoryLocked() );
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

    auto &center = TaskCenter::instance();

    // Seed the step plans with the pipeline's task ids so the checkpoint
    // written BEFORE dispatch already maps steps to tasks (crash during
    // submit still leaves a resumable picture).
    const long pipelineId = center.submitPipeline( def, autoLoad );
    if ( pipelineId < 0 )
    {
        run->setErrorMessage( "Pipeline contains no dispatchable operator steps" );
        run->transitionTo( WorkflowRunState::Failed );
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            persistRunLocked( *run );
        }
        return -1;
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
                if ( info.status == sicnu::TaskStatus::Completed
                     && !info.outputLayerPath.isEmpty() )
                {
                    plan->outputLayerPath = info.outputLayerPath.toStdString();
                    run->setArtifact( stepId, plan->outputLayerPath );
                }
                run->updateStepPlan( *plan );
            }
        }
        persistRunLocked( *run );

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
            finalizeRunLocked( pipelineId, *run );
    }
    return pipelineId;
}

void WorkflowRunCoordinator::onTaskUpdated( const AlgorithmTaskInfo &info )
{
    if ( info.pipelineId < 0 || info.stepId.isEmpty() )
        return;

    std::shared_ptr<WorkflowRun> run;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        const auto it = m_runsByPipeline.find( info.pipelineId );
        if ( it == m_runsByPipeline.end() )
            return;
        run = it->second;
    }

    const std::string stepKey = info.stepId.toStdString();
    std::optional<StepPlan> plan = run->stepPlan( stepKey );
    if ( !plan )
        return;

    plan->status = stepStatusForTaskStatus( info.status ).toStdString();
    plan->taskId = info.taskId;
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
        }
    }
    else if ( info.status == sicnu::TaskStatus::Failed )
    {
        plan->errorMessage = info.errorMessage.toStdString();
    }
    run->updateStepPlan( *plan );

    std::lock_guard<std::mutex> lock( m_mutex );
    // The map entry is ours for the process lifetime; re-check it survived
    // (clearCompletedTasks on TaskCenter does not touch our bookkeeping).
    if ( m_runsByPipeline.count( info.pipelineId ) == 0 )
        return;
    persistRunLocked( *run );

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
        finalizeRunLocked( info.pipelineId, *run ); // exactly once per run
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
    persistRunLocked( run );

    if ( run.state() == WorkflowRunState::Completed )
    {
        // Intermediate artifacts are reaped only for completed runs (resume
        // and retry depend on the intermediates of unfinished ones). Deletion
        // failures are collected by the GC report; surfaced via the run error
        // field would mislabel a completed run, so they are logged only.
        ArtifactGC gc;
        gc.sweepRun( run, /*retainFinalOutputs=*/true );
        // The run is finished and its outputs are committed assets; the
        // checkpoint has served its purpose. Failed/Canceled/Interrupted
        // checkpoints are KEPT — they are the resume handle.
        QFile::remove( checkpointPathLocked( run.runId() ) );
    }
    (void)pipelineId;
}

WorkflowRunCoordinator::RecoveryReport WorkflowRunCoordinator::recoverAtStartup( bool autoResume )
{
    RecoveryReport report;
    std::vector<std::shared_ptr<WorkflowRun>> recovered;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        // checkpointDirectory() relocks m_mutex (non-recursive): use the
        // locked variant or startup self-deadlocks (review P0).
        recovered = m_checkpoints.recoverInterruptedRuns( checkpointDirectoryLocked() );
    }
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
    QString loadErr;
    std::shared_ptr<WorkflowRun> run;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        run = m_checkpoints.loadCheckpoint( path, &loadErr );
    }
    if ( !run )
    {
        if ( error )
            *error = QStringLiteral( "Checkpoint rejected for run %1: %2" )
                       .arg( QString::fromStdString( runId ), loadErr );
        return -1;
    }
    if ( run->state() != WorkflowRunState::Interrupted && run->state() != WorkflowRunState::Failed
         && run->state() != WorkflowRunState::Canceled )
    {
        if ( error )
            *error = QStringLiteral( "Run %1 is %2 — only interrupted/failed/canceled runs resume" )
                       .arg( QString::fromStdString( runId ),
                             QString::fromStdString( workflowRunStateToString( run->state() ) ) );
        return -1;
    }

    // Steps already completed whose outputs still exist are pre-resolved into
    // the remaining steps' parameters; everything else runs again.
    std::map<std::string, std::string> completedOutputs;
    for ( const auto &plan : run->stepPlans() )
    {
        if ( plan.status != "Completed" || plan.outputLayerPath.empty() )
            continue;
        if ( QFileInfo::exists( QString::fromStdString( plan.outputLayerPath ) ) )
            completedOutputs[plan.stepId] = plan.outputLayerPath;
    }

    WorkflowDefinition def = run->definition();
    WorkflowDefinition remaining;
    remaining.id = def.id + "_resume";
    remaining.title = def.title;
    for ( const auto &step : def.steps )
    {
        const auto planIt = std::find_if( run->stepPlans().begin(), run->stepPlans().end(),
                                          [&]( const StepPlan &p ) { return p.stepId == step.id; } );
        const bool reusable = planIt != run->stepPlans().end() && planIt->status == "Completed"
                              && completedOutputs.count( step.id ) > 0;
        if ( reusable )
            continue; // served from its persisted output — never re-executed

        StepDef resumed = step;
        auto resolver = [&]( const PlaceholderRef &ref ) -> std::string {
            const auto it = completedOutputs.find( ref.stepId );
            if ( it != completedOutputs.end() )
                return it->second;
            return ref.rawRef; // unresolved (live parent ref): TaskCenter resolves at dispatch
        };
        bool changed = false;
        resumed.params = substituteJsonPlaceholders( step.params, resolver, &changed );
        // Edges to reusable steps are dropped with the step itself: TaskCenter
        // only wires parent links for steps present in this submission, the
        // literal substitution above already carries the data forward, and
        // the topological sort must not see references to excluded steps.
        // Keep only connections whose source step is ALSO being resubmitted;
        // edges from pre-resolved steps were consumed by the substitution and
        // would otherwise reference steps absent from the resume submission.
        StepDef withLiveEdges = resumed;
        withLiveEdges.inputs.clear();
        for ( const auto &conn : resumed.inputs )
        {
            const bool sourceResubmitted = std::any_of(
                remaining.steps.begin(), remaining.steps.end(),
                [&]( const StepDef &kept ) { return kept.id == conn.fromStepId; } );
            if ( sourceResubmitted )
                withLiveEdges.inputs.push_back( conn );
        }
        remaining.steps.push_back( withLiveEdges );
    }
    if ( remaining.steps.empty() )
    {
        if ( error )
            *error = QStringLiteral( "Run %1 has nothing left to resume" )
                       .arg( QString::fromStdString( runId ) );
        return -1;
    }

    if ( !run->transitionTo( WorkflowRunState::Running ) )
        run->forceSetState( WorkflowRunState::Running );
    const long pipelineId = startTrackedPipeline( remaining, /*autoLoad=*/true );
    if ( pipelineId < 0 )
    {
        run->forceSetState( WorkflowRunState::Failed );
        run->setErrorMessage( "Resume submission failed" );
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            persistRunLocked( *run );
        }
        if ( error )
            *error = QStringLiteral( "Resume submission failed for run %1" )
                       .arg( QString::fromStdString( runId ) );
        return -1;
    }
    // startTrackedPipeline created a FRESH run for the resume submission;
    // swap in the ORIGINAL interrupted run so one runId carries the whole
    // lineage and a second interruption resumes from the union of both
    // passes (its Completed step plans + artifacts are already inside).
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        const auto it = m_runsByPipeline.find( pipelineId );
        if ( it != m_runsByPipeline.end() )
        {
            // Keep the fresh submission's live task ids on the original plans.
            for ( const auto &fresh : it->second->stepPlans() )
            {
                if ( StepPlan *plan = run->findStepPlan( fresh.stepId ) )
                {
                    plan->taskId = fresh.taskId;
                    if ( plan->status != "Completed" )
                        plan->status = "Pending";
                    run->updateStepPlan( *plan );
                }
            }
            m_pipelineByRunId.erase( it->second->runId() );
            // The fresh submission persisted a checkpoint under ITS runId
            // before the swap; recovery would resurrect it as a ghost
            // Interrupted run duplicating this resume (review P1).
            QFile::remove( checkpointPathLocked( it->second->runId() ) );
            it->second = run;
            m_pipelineByRunId[run->runId()] = pipelineId;
            persistRunLocked( *run );
        }
    }
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
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        persistRunLocked( *run );
    }
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

std::vector<std::shared_ptr<WorkflowRun>> WorkflowRunCoordinator::runs() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    std::vector<std::shared_ptr<WorkflowRun>> out;
    out.reserve( m_runsByPipeline.size() );
    for ( const auto &kv : m_runsByPipeline )
        out.push_back( kv.second );
    return out;
}

} // namespace sicnu::workflow
