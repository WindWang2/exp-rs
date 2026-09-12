// checkpoint_evidence.cpp — see checkpoint_evidence.h. Moved verbatim from
// WorkflowExperimentMonitor::reconcileStaleRuns so the lab recorder closes
// crash-orphaned runs from the same checkpoint truth.
#include "checkpoint_evidence.h"

#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run_coordinator.h"
#include "workflow/workflow_run_lock.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace sicnu::experiment
{

std::function<std::optional<ExecutionEvidence>( const QString &executionRef )>
checkpointEvidenceLookup( workflow::WorkflowRunCoordinator &coordinator )
{
    return [&coordinator]( const QString &executionRef )
        -> std::optional<ExecutionEvidence> {
        if ( executionRef.isEmpty() )
            return std::nullopt;

        const QString directory = coordinator.checkpointDirectory();

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
}

} // namespace sicnu::experiment
