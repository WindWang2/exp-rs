// workflow_experiment_adapter.h — WorkflowRunCoordinator → ExperimentRunBridge
// adapter (goal 8.0 §A). This is the workflow-side half of the bridge: it
// converts the authoritative WorkflowRun aggregate into the neutral
// ExecutionEvent vocabulary (see src/experiment/run_bridge.h) and records
// enabled submissions into an ExperimentRunBridge.
//
// Layering: this target (sicnu_experiment_bridge) links Sicnu::experiment +
// sicnu_workflow. Nothing in the workflow stack links back — the coordinator
// stays experiment-free, and the monitor is a pure consumer of its public
// signal/read APIs. It executes nothing and never mutates a run.
//
// Truthfulness contract: only the coordinator's persisted state transitions
// are recorded. Transitional workflow states that carry no experiment
// meaning (Created/Planning/Ready/WaitingResource/Cancelling) are ignored —
// ignoring a non-terminal transition records nothing; it never fabricates
// anything. Terminal/Interrupted/Running states map 1:1 onto the bridge's
// closed vocabulary.
//
// Threading: the monitor MUST live on the thread that submits/resumes
// workflows (the Qt object's thread()). The ghost-suppression gate and the
// pin-at-submission ordering rely on queued coordinator deliveries arriving
// after the synchronous submit/resume path has completed — which holds only
// under that affinity (asserted in enable()/recordSubmission()).
//
// Recording scope: EXPLICITLY ENABLED SUBMISSIONS only. A submission is
// recorded when recordSubmission() is called for its run (the opt-in
// surface does this right after startTrackedPipeline returns); signal-driven
// events for any other run are ignored. Enabling the monitor never makes the
// process record unconsented runs.
#pragma once

#include <QObject>
#include <QString>

#include "dataset/dataset_store.h"

#include "experiment/run_bridge.h"

#include <memory>

namespace sicnu::workflow
{
class WorkflowRun;
class WorkflowRunCoordinator;
} // namespace sicnu::workflow

namespace sicnu::experiment
{

/// Process-level monitor wiring the coordinator lifecycle to a bridge.
/// Inert until enable() + recordSubmission() are called by an opt-in
/// surface (MCP run_workflow recording arguments today).
class WorkflowExperimentMonitor : public QObject
{
    Q_OBJECT
  public:
    /// Connects to the coordinator singleton. Does not enable recording.
    explicit WorkflowExperimentMonitor( workflow::WorkflowRunCoordinator &coordinator,
                                        QObject *parent = nullptr );
    ~WorkflowExperimentMonitor() override;

    WorkflowExperimentMonitor( const WorkflowExperimentMonitor & ) = delete;
    WorkflowExperimentMonitor &operator=( const WorkflowExperimentMonitor & ) = delete;

    /// Opens the experiment store (creating the DB file when missing —
    /// callers pass an explicit path, so creation is an informed act) and
    /// idempotently ensures the target experiment. The monitor is bound to
    /// ONE experiment db: re-enabling with a DIFFERENT path is a typed
    /// refusal (silently recording into the first db would misplace
    /// records). Re-enabling with the same db/experiment updates pins and
    /// metadata, not an error.
    bool enable( const QString &experimentDbPath, const QString &experimentId,
                 const QString &experimentName, const QString &objective,
                 const QString &datasetDbPath, QString *error = nullptr );

    bool isEnabled() const;
    /// Pre-start pin registry (library-level API; MCP-style surfaces call
    /// recordSubmission with the pins directly instead).
    void setWorkflowPins( const QString &workflowId, const RunPins &pins );

    /// Records ONE enabled submission: synchronously drives the bridge with
    /// the run's Running transition and @p pins (identity pins are resolved
    /// at start — immune to later signal reordering). @p run must be the run
    /// tracked for the just-submitted pipeline; only this run (and its
    /// resume story under the same run id) is recorded.
    /// Returns the experiment run id.
    Result<QString> recordSubmission( const workflow::WorkflowRun &run, const RunPins &pins );

    /// Opts an ALREADY-RECORDED execution's continuation into recording
    /// (e.g. a resume surface continuing a story that a prior submission
    /// started). Never creates a new record: events for unknown refs are
    /// refused by the bridge — which is the honest behavior for an
    /// execution nobody ever recorded.
    void optInResume( const QString &executionRef );

    /// CLI-shutdown recording path: converts the coordinator's CURRENT run
    /// aggregate into a lifecycle event and records it. Content-identical to
    /// what the queued runStateChanged delivery would carry (same snapshot
    /// code path); terminal/Interrupted states are recorded, transitional
    /// states are ignored (never recorded as anything). For refs this
    /// monitor never enabled the bridge refuses — no fabricated history.
    Result<QString> recordAggregateState( const workflow::WorkflowRun &run );

    /// Startup/stale reconciliation: runs recorded as non-terminal whose
    /// execution is neither tracked by this process nor owned by a live
    /// process (flock probe) get closed according to their checkpoint
    /// evidence. Called automatically by enable(); returns the decisions
    /// taken (also logged).
    QVector<ExperimentRunBridge::StaleDecision> reconcileStaleRuns();

    /// Drains queued runStateChanged deliveries into the bridge (call at
    /// shutdown when an event loop may not pump again). No-op when disabled.
    void flush();

  private slots:
    void onRunStateChanged( const QString &runId, const QString &workflowId,
                            const QString &state, qint64 startedMs, qint64 finishedMs );

  private:
    QString checkpointPathFor( const QString &runId ) const;

    workflow::WorkflowRunCoordinator &m_coordinator;
    ExperimentStore m_store;
    std::unique_ptr<ExperimentRunBridge> m_bridge;
    /// Optional pin-verification store; owned here because enable() receives
    /// only a path. A db that fails to open disables verification, never
    /// recording.
    std::unique_ptr<sicnu::dataset::DatasetStore> m_datasetStore;
    /// The experiment db this monitor is bound to (empty until enable()).
    QString m_experimentDbPath;
    /// Run ids of ENABLED submissions — the only executions this monitor
    /// records. Anything else on the signal path is ignored (per-submission
    /// opt-in, never process-wide recording).
    QSet<QString> m_enabledRefs;
    bool m_connected = false;
};

} // namespace sicnu::experiment
