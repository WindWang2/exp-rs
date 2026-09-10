// workflow_experiment_adapter.h — WorkflowRunCoordinator → ExperimentRunBridge
// adapter (goal 8.0 §A). This is the workflow-side half of the bridge: it
// converts the authoritative WorkflowRun aggregate into the neutral
// ExecutionEvent vocabulary (see src/experiment/run_bridge.h) and connects
// the coordinator's runStateChanged signal to an ExperimentRunBridge.
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

/// Converts one persisted workflow-run snapshot + state transition into a
/// bridge event. Pure function; bounded output (step summaries are capped by
/// the bridge's evidence limit, and resolved parameters stay in the
/// checkpoint — the event cites, it does not duplicate).
ExecutionEvent workflowRunToExecutionEvent( const workflow::WorkflowRun &run,
                                            const QString &state, qint64 startedMs,
                                            qint64 finishedMs );

/// Process-level monitor wiring the coordinator signal to a bridge. Enabled
/// lazily by an opt-in surface (MCP run_workflow recording args today; other
/// surfaces may attach their own instance). Disabled by default: with no
/// enable() call the monitor records nothing anywhere.
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
    /// idempotently ensures the target experiment. Returns false + @a error
    /// when the store cannot serve. Re-enabling with the same experiment is
    /// an update of pins/name, not an error.
    bool enable( const QString &experimentDbPath, const QString &experimentId,
                 const QString &experimentName, const QString &objective,
                 const QString &datasetDbPath, QString *error = nullptr );

    bool isEnabled() const;
    /// Pre-submission pin registry (forwarded to the bridge; must happen
    /// before the tracked run starts to be effective).
    void setWorkflowPins( const QString &workflowId, const RunPins &pins );

    /// Startup/stale reconciliation at enable time: runs recorded as
    /// non-terminal whose execution is neither tracked by this process nor
    /// owned by a live process (flock probe) get closed according to their
    /// checkpoint evidence. Returns the decisions taken (also logged).
    QVector<ExperimentRunBridge::StaleDecision> reconcileStaleRuns();

    /// Drains queued runStateChanged deliveries into the bridge (call at
    /// shutdown when an event loop may not pump again). No-op when disabled.
    void flush();

  private slots:
    void onRunStateChanged( const QString &runId, const QString &workflowId,
                            const QString &state, qint64 startedMs, qint64 finishedMs );

  private:
    workflow::WorkflowRunCoordinator &m_coordinator;
    ExperimentStore m_store;
    std::unique_ptr<ExperimentRunBridge> m_bridge;
    /// Optional pin-verification store; owned here because enable() receives
    /// only a path. A db that fails to open disables verification, never
    /// recording.
    std::unique_ptr<sicnu::dataset::DatasetStore> m_datasetStore;
    bool m_connected = false;
};

} // namespace sicnu::experiment
