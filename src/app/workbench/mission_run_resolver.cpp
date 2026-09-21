/***************************************************************************
 * mission_run_resolver.cpp — live execution state for mission run refs
 ***************************************************************************/

#include "app/workbench/mission_run_resolver.h"

#include "processing/framework/task_center.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include <QString>

namespace sicnu::app
{

namespace
{

MissionRunStatus fromTaskStatus( sicnu::TaskStatus status, const QString &detail )
{
    MissionRunStatus s;
    switch ( status )
    {
        case sicnu::TaskStatus::Queued:
        case sicnu::TaskStatus::Running:
        case sicnu::TaskStatus::Paused:
        case sicnu::TaskStatus::WaitingResource:
        case sicnu::TaskStatus::Dispatching:
        case sicnu::TaskStatus::Cancelling:
            s.liveness = MissionRunLiveness::Alive;
            s.stateKey = QStringLiteral( "in_flight" );
            break;
        case sicnu::TaskStatus::Completed:
            s.liveness = MissionRunLiveness::TerminalSuccess;
            s.stateKey = QStringLiteral( "completed" );
            break;
        case sicnu::TaskStatus::Failed:
            s.liveness = MissionRunLiveness::TerminalFailure;
            s.stateKey = QStringLiteral( "failed" );
            break;
        case sicnu::TaskStatus::Canceled:
            s.liveness = MissionRunLiveness::Canceled;
            s.stateKey = QStringLiteral( "canceled" );
            break;
    }
    s.detail = detail;
    return s;
}

MissionRunStatus fromRunState( sicnu::workflow::WorkflowRunState state, const QString &detail )
{
    MissionRunStatus s;
    switch ( state )
    {
        case sicnu::workflow::WorkflowRunState::Created:
        case sicnu::workflow::WorkflowRunState::Planning:
        case sicnu::workflow::WorkflowRunState::Ready:
        case sicnu::workflow::WorkflowRunState::Running:
        case sicnu::workflow::WorkflowRunState::WaitingResource:
        case sicnu::workflow::WorkflowRunState::Cancelling:
            s.liveness = MissionRunLiveness::Alive;
            s.stateKey = QStringLiteral( "in_flight" );
            break;
        case sicnu::workflow::WorkflowRunState::Interrupted:
            // #1168: the workflow layer itself treats Interrupted as a
            // terminal recovery state — nothing executes. Mapping it to
            // Alive made the reconcile count the task leftRunning and
            // mission:advance report a fake Running; Unknown routes the
            // task through the fail-closed Stale/retryable path instead.
            s.liveness = MissionRunLiveness::Unknown;
            s.stateKey = QStringLiteral( "interrupted" );
            break;
        case sicnu::workflow::WorkflowRunState::Completed:
            s.liveness = MissionRunLiveness::TerminalSuccess;
            s.stateKey = QStringLiteral( "completed" );
            break;
        case sicnu::workflow::WorkflowRunState::Failed:
            s.liveness = MissionRunLiveness::TerminalFailure;
            s.stateKey = QStringLiteral( "failed" );
            break;
        case sicnu::workflow::WorkflowRunState::Canceled:
            s.liveness = MissionRunLiveness::Canceled;
            s.stateKey = QStringLiteral( "canceled" );
            break;
    }
    s.detail = detail;
    return s;
}

bool parseLongId( const QString &id, long &out )
{
    bool ok = false;
    const long value = id.toLong( &ok );
    if ( !ok || value < 0 )
        return false;
    out = value;
    return true;
}

} // namespace

MissionRunStatus resolveMissionRunStatus( const MissionRunRef &ref )
{
    MissionRunStatus status;
    if ( ref.isNull() )
    {
        status.liveness = MissionRunLiveness::Unknown;
        status.stateKey = QStringLiteral( "missing_run_reference" );
        return status;
    }

    if ( ref.kind == QLatin1String( "task_center" ) )
    {
        long taskId = -1;
        if ( !parseLongId( ref.id, taskId ) )
        {
            status.liveness = MissionRunLiveness::Unknown;
            status.stateKey = QStringLiteral( "malformed_task_id" );
            return status;
        }
        const sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
        if ( info.taskId < 0 )
        {
            // Not (or no longer) known to the authority: a crash residue.
            status.liveness = MissionRunLiveness::Unknown;
            status.stateKey = QStringLiteral( "task_not_found" );
            return status;
        }
        return fromTaskStatus( info.status, info.errorMessage.left( 200 ) );
    }

    if ( ref.kind == QLatin1String( "workflow_run" ) || ref.kind == QLatin1String( "pipeline_run" ) )
    {
        auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
        long pipelineId = -1;
        if ( ref.kind == QLatin1String( "pipeline_run" ) )
        {
            if ( !parseLongId( ref.id, pipelineId ) )
            {
                status.liveness = MissionRunLiveness::Unknown;
                status.stateKey = QStringLiteral( "malformed_pipeline_id" );
                return status;
            }
        }
        else
        {
            pipelineId = coordinator.pipelineIdForRun( ref.id.toStdString() );
            if ( pipelineId < 0 )
            {
                status.liveness = MissionRunLiveness::Unknown;
                status.stateKey = QStringLiteral( "run_not_found" );
                return status;
            }
        }
        const std::shared_ptr<sicnu::workflow::WorkflowRun> run =
            coordinator.runForPipeline( pipelineId );
        if ( !run )
        {
            status.liveness = MissionRunLiveness::Unknown;
            status.stateKey = QStringLiteral( "run_not_found" );
            return status;
        }
        return fromRunState( run->state(), QString::fromStdString( run->runId() ) );
    }

    status.liveness = MissionRunLiveness::Unknown;
    status.stateKey = QStringLiteral( "unsupported_run_kind" );
    return status;
}

} // namespace sicnu::app
