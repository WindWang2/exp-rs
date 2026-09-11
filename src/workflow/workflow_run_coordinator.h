// src/workflow/workflow_run_coordinator.h — production wiring for Workflow v2
//
// Bridges the TaskCenter in-memory pipeline execution (the seam every
// production surface uses: GUI panel, agent plans, MCP run_workflow, CLI)
// to the persistent Workflow Engine 2.0 aggregate: per-transition atomic
// checkpoints, crash recovery, resumable interrupted runs, and ArtifactGC
// on completion (#697 / #668 — the checkpoint/recovery/GC subsystem used
// to have zero production callers).
#pragma once

#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QObject>

#include "workflow_checkpoint.h"
#include "workflow_definition.h"
#include "workflow_run.h"
#include "workflow_run_lock.h"

namespace sicnu {

struct AlgorithmTaskInfo; // match task_center.h definition tag (MSVC mangles class/struct differently)

namespace workflow {

/// Orchestrates the lifecycle of one tracked pipeline run:
/// TaskCenter pipeline -> live WorkflowRun updates -> checkpoint -> GC.
class WorkflowRunCoordinator : public QObject {
    Q_OBJECT
  public:
    struct RecoveryReport
    {
        int interruptedRuns = 0;  // runs transitioned to Interrupted + re-saved
        int resumedPipelines = 0; // runs actually resubmitted (autoResume only)
        QStringList runIds;
        QStringList errors;
    };

    static WorkflowRunCoordinator &instance();

    /// Submit a pipeline through TaskCenter while tracking it as a persisted
    /// WorkflowRun: checkpoint saved before dispatch, every task transition is
    /// folded into the run and persisted atomically, and a completed run is
    /// swept by ArtifactGC. Returns the TaskCenter pipelineId (> 0), or -1.
    long startTrackedPipeline( const WorkflowDefinition &def, bool autoLoad = true );

    /// JSON flavor (MCP run_workflow): parses the pipeline JSON with the same
    /// rules as TaskCenter::submitPipelineJson and tracks it. -1 on a parse
    /// error (the caller reports the expected shape).
    long startTrackedPipelineJson( const std::string &jsonPipeline, bool autoLoad = true );

    /// Startup recovery (#697): mark non-terminal runs Interrupted (steps
    /// stuck Running/Cancelling reset to Pending) and optionally resubmit the
    /// remaining work. With @a autoResume false the runs stay resumable via
    /// resumeRun — nothing is silently re-executed.
    RecoveryReport recoverAtStartup( bool autoResume = false );

    /// Resume an Interrupted/Failed/Canceled run: steps whose recorded output
    /// still exists on disk are NOT re-executed — their outputs are resolved
    /// into the remaining steps' placeholder parameters ($stepId.port), a new
    /// tracked pipeline is submitted for the rest. Returns its pipelineId or
    /// -1 (@a error explains why).
    long resumeRun( const std::string &runId, QString *error = nullptr );

    /// Cancel a tracked run's pipeline (delegates to TaskCenter::cancelPipeline;
    /// the run state rolls up from the step outcomes).
    bool cancelRun( long pipelineId );

    /// Snapshot of the run tracked for @a pipelineId (null when untracked).
    /// WorkflowRun is mutex-guarded and non-copyable, so the shared run is
    /// handed out directly; its internal locking keeps concurrent reads safe.
    std::shared_ptr<WorkflowRun> runForPipeline( long pipelineId ) const;

    /// Current TaskCenter pipeline id tracked for @a runId (-1 when none).
    long pipelineIdForRun( const std::string &runId ) const;

    /// All runs tracked by this process (live + terminal).
    std::vector<std::shared_ptr<WorkflowRun>> runs() const;

    /// Checkpoint directory override (tests); empty restores the default
    /// (~/.rs_studio/checkpoints, see WorkflowCheckpointManager).
    void setCheckpointDirectory( const QString &directory );
    QString checkpointDirectory() const;

    /// Emitted on tracked-run lifecycle transitions — Running when a tracked
    /// pipeline starts, the real terminal state (Completed/Failed/Canceled)
    /// at finalize, and Interrupted when crash recovery reconciles the run.
    /// Mirrored after every persisted transition so terminal/Interrupted
    /// entries carry truthful started/finished ms (issue #754).
    /// Governance (WorkspaceService::recordRun) subscribes so the runs index
    /// reflects what actually happened instead of fabricating states.
    /// Delivery is a Qt signal (lifetime-managed, queueable) because the
    /// coordinator may emit with its internal mutex held: receivers must not
    /// call back into the coordinator synchronously.
    /// Mirrored after every persisted run-state transition (issue #754):
    /// terminal/Interrupted transitions carry truthful started/finished ms.
    /// One emission per tracked-run state transition.
    //  (Fresh-build repair: churn on master had declared this signal three
    //  times across three signals: sections — moc emitted one body per
    //  declaration and the translation unit failed with C2084.)
  signals:
    void runStateChanged( const QString &runId, const QString &workflowId,
                          const QString &state, qint64 startedMs, qint64 finishedMs );

  private slots:
    void onTaskUpdated( const sicnu::AlgorithmTaskInfo &info );

  private:
    WorkflowRunCoordinator();
    ~WorkflowRunCoordinator() override;
    WorkflowRunCoordinator( const WorkflowRunCoordinator & ) = delete;
    WorkflowRunCoordinator &operator=( const WorkflowRunCoordinator & ) = delete;

    /// Emits runStateChanged for @p run's current state. Requires m_mutex
    /// held (reads the run; emission is the last thing before unlocking).
    /// Falls back to the run's creation stamp when @a startedMs <= 0
    /// (issue #754). Callers queue across threads (queued connection in
    /// ProjectContext).
    void notifyRunStateLocked( const WorkflowRun &run, qint64 startedMs, qint64 finishedMs );
    void persistRunLocked( WorkflowRun &run );
    /// Terminal roll-up + ArtifactGC + checkpoint retention. Called with
    /// m_mutex held when the last step of a tracked run went terminal.
    void finalizeRunLocked( long pipelineId, WorkflowRun &run );
    /// m_mutex-free directory read for call paths that already hold it.
    QString checkpointDirectoryLocked() const;
    QString checkpointPathLocked( const std::string &runId ) const;
    QString checkpointPathFor( const std::string &runId ) const;

    mutable std::recursive_mutex m_mutex;
    WorkflowCheckpointManager m_checkpoints;
    QString m_checkpointDir; // empty → defaultCheckpointDirectory()
    std::map<long, std::shared_ptr<WorkflowRun>> m_runsByPipeline;
    std::map<std::string, long> m_pipelineByRunId;
    std::set<std::string> m_resuming; ///< concurrent resumeRun guard
    /// Cross-process run ownership (#727): one held lock per executing run,
    /// released at finalize / resume swap / submission failure.
    std::map<std::string, std::shared_ptr<WorkflowRunLock>> m_locksByRunId;
    bool m_connected = false;
};

} // namespace workflow
} // namespace sicnu
