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
#include <string>
#include <vector>

#include <QObject>

#include "workflow_checkpoint.h"
#include "workflow_definition.h"
#include "workflow_run.h"

namespace sicnu {

class AlgorithmTaskInfo;

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

    /// All runs tracked by this process (live + terminal).
    std::vector<std::shared_ptr<WorkflowRun>> runs() const;

    /// Checkpoint directory override (tests); empty restores the default
    /// (~/.rs_studio/checkpoints, see WorkflowCheckpointManager).
    void setCheckpointDirectory( const QString &directory );
    QString checkpointDirectory() const;

  private slots:
    void onTaskUpdated( const sicnu::AlgorithmTaskInfo &info );

  private:
    WorkflowRunCoordinator();
    ~WorkflowRunCoordinator() override;
    WorkflowRunCoordinator( const WorkflowRunCoordinator & ) = delete;
    WorkflowRunCoordinator &operator=( const WorkflowRunCoordinator & ) = delete;

    /// Requires m_mutex held (uses the no-lock directory accessor).
    void persistRunLocked( WorkflowRun &run );
    /// Terminal roll-up + ArtifactGC + checkpoint retention. Called with
    /// m_mutex held when the last step of a tracked run went terminal.
    void finalizeRunLocked( long pipelineId, WorkflowRun &run );
    /// m_mutex-free directory read for call paths that already hold it.
    QString checkpointDirectoryLocked() const;
    QString checkpointPathLocked( const std::string &runId ) const;
    QString checkpointPathFor( const std::string &runId ) const;

    mutable std::mutex m_mutex;
    WorkflowCheckpointManager m_checkpoints;
    QString m_checkpointDir; // empty → defaultCheckpointDirectory()
    std::map<long, std::shared_ptr<WorkflowRun>> m_runsByPipeline;
    std::map<std::string, long> m_pipelineByRunId;
    bool m_connected = false;
};

} // namespace workflow
} // namespace sicnu
