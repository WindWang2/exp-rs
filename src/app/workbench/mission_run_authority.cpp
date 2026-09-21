/***************************************************************************
 * mission_run_authority.cpp — run-authority resolution + reconciliation
 ***************************************************************************/

#include "app/workbench/mission_run_authority.h"

namespace sicnu::app
{

MissionRunStatus resolveMissionRunUnresolved( const MissionRunRef & )
{
    MissionRunStatus status;
    status.liveness = MissionRunLiveness::Unknown;
    status.stateKey = QStringLiteral( "unresolved" );
    return status;
}

MissionRunReconciliation reconcileRunAuthority( MissionTimeline &timeline,
                                                const MissionRunStatusResolver &resolver,
                                                const QString &iso )
{
    MissionRunReconciliation report;

    for ( const MissionTask &task : timeline.tasks() )
    {
        if ( task.status != MissionTaskStatus::Running )
            continue;

        QString reason;
        MissionTaskStatus target = MissionTaskStatus::Running;
        bool hasTarget = false;

        if ( task.run.isNull() )
        {
            // A Running task with no bound run can only be a crash residue.
            reason = QStringLiteral( "stale_run_reference" );
            target = MissionTaskStatus::Stale;
            hasTarget = true;
        }
        else
        {
            const MissionRunStatus status =
                resolver ? resolver( task.run ) : resolveMissionRunUnresolved( task.run );
            switch ( status.liveness )
            {
                case MissionRunLiveness::Alive:
                    ++report.leftRunning;
                    break;
                case MissionRunLiveness::TerminalSuccess:
                    target = MissionTaskStatus::Succeeded;
                    hasTarget = true;
                    reason = QStringLiteral( "run_succeeded" );
                    break;
                case MissionRunLiveness::TerminalFailure:
                    target = MissionTaskStatus::Failed;
                    hasTarget = true;
                    reason = QStringLiteral( "run_failed" );
                    break;
                case MissionRunLiveness::Canceled:
                    target = MissionTaskStatus::Canceled;
                    hasTarget = true;
                    reason = QStringLiteral( "run_canceled" );
                    break;
                case MissionRunLiveness::Unknown:
                    // Fail closed: an unverifiable run is not a running one.
                    target = MissionTaskStatus::Stale;
                    hasTarget = true;
                    reason = QStringLiteral( "run_authority_unresolved" );
                    break;
            }
        }

        if ( !hasTarget )
            continue;

        const MissionOutcome outcome =
            timeline.transition( task.id, target, iso,
                                 QStringLiteral( "run_authority_reconciled:%1" ).arg( reason ) );
        if ( !outcome.applied )
        {
            // The state machine refused (e.g. an illegal pair): keep the task
            // as it is and record why — never force a status around the table.
            report.details.append(
                QStringLiteral( "%1:rejected:%2" ).arg( task.id, outcome.reason ) );
            continue;
        }

        switch ( target )
        {
            case MissionTaskStatus::Running:
                break;
            case MissionTaskStatus::Succeeded:
                ++report.succeededFromRun;
                break;
            case MissionTaskStatus::Failed:
                ++report.failedFromRun;
                break;
            case MissionTaskStatus::Canceled:
                ++report.canceledFromRun;
                break;
            default:
                ++report.staleFromRun;
                break;
        }
        report.details.append( QStringLiteral( "%1:%2" ).arg( task.id, reason ) );
    }

    return report;
}

} // namespace sicnu::app
