// lab_run_recorder.h — desktop lab auto-recording surface (D5, ADR 0143
// semantics).
//
// The governance rule of ADR 0143 holds unchanged: recording attaches to the
// authoritative WorkflowRunCoordinator lifecycle, executes nothing, and
// fabricates nothing. The DIFFERENCE from WorkflowExperimentMonitor is the
// consent shape, not the semantics: the MCP/CLI surfaces opt in PER
// SUBMISSION from their own submission path; the desktop lab has no such
// path (the session controller is deliberately untouched by this track), so
// LabRunRecorder opts a run in when the coordinator itself reports it
// Running — the same authoritative signal, received queued, in order. Every
// lab execution therefore becomes a first-class experiment run by default
// (goal D5: opt-out, never opt-in); setRecordingEnabled(false) stops NEW
// stories from being recorded, while stories already recorded still receive
// their truthful terminal events.
//
// Truthfulness contract (mirrors the bridge's):
//   - only states in bridgeExecutionStates() are acted on;
//   - a run is recorded STARTING at its Running event — terminal events for
//     executions this recorder never saw start are refused by the bridge
//     (no back-filled history) and terminal events for foreign executions
//     are ignored by this recorder;
//   - the coordinator's checkpoint ghost-suppression rule is inherited: an
//     untracked Running event is only recorded if its checkpoint exists.
//
// Evidence injection: at terminal/Interrupted events the recorder snapshots
// the operation trail (RSOperationLogger records, injected as a callback so
// this science-side target never links the operator stack), REDACTS it
// through RunEnvironment::redactSecretKeys and caps it (ADR 0143 bounded
// evidence) before handing it to the bridge as `extra.operationTrail`. The
// bridge stores it inside the run's workflow evidence document.
#pragma once

#include "../data/data_result.h"
#include "dataset/dataset_store.h"
#include "experiment/experiment_store.h"
#include "experiment/run_bridge.h"

#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>

namespace sicnu::workflow
{
class WorkflowRunCoordinator;
}

namespace sicnu::experiment
{

class LabRunRecorder : public QObject
{
    Q_OBJECT
  public:
    /// Binds to the coordinator singleton. Recording starts only after a
    /// successful enable().
    explicit LabRunRecorder( workflow::WorkflowRunCoordinator &coordinator,
                             QObject *parent = nullptr );
    ~LabRunRecorder() override;

    LabRunRecorder( const LabRunRecorder & ) = delete;
    LabRunRecorder &operator=( const LabRunRecorder & ) = delete;

    /// Opens (creating when missing — an informed act, like the monitor's)
    /// the experiment db and idempotently ensures the target experiment,
    /// then connects to the coordinator (queued — it emits with its mutex
    /// held) and reconciles stale runs from checkpoint evidence. Binding to
    /// a SECOND db path is a typed refusal. Returns false with @p error set.
    bool enable( const QString &experimentDbPath, const QString &experimentId,
                 const QString &experimentName, const QString &objective,
                 const QString &datasetDbPath = QString(), QString *error = nullptr );

    /// Whether the recorder is bound to a store (events flow only when this
    /// AND recordingEnabled() hold).
    bool isBound() const;
    /// Opt-out switch (D5 default: ON). Disabling stops NEW runs from being
    /// recorded; already-recorded stories still close truthfully.
    void setRecordingEnabled( bool enabled );
    bool recordingEnabled() const;

    /// Identity pins for workflows whose dataset/model identity a caller
    /// knows BEFORE the run starts (pre-submission registry passthrough).
    void setWorkflowPins( const QString &workflowId, const RunPins &pins );
    /// Per-execution override; effective only before the run started.
    Result<void> attachExecutionPins( const QString &executionRef, const RunPins &pins );

    /// The operation-trail evidence source. Invoked synchronously at
    /// terminal/Interrupted handling; the returned records are redacted and
    /// capped before persisting. Unset = no trail evidence.
    using OperationTrailSource = std::function<QJsonArray()>;
    void setOperationTrailSource( OperationTrailSource source );

    QString experimentId() const;
    QString experimentDbPath() const;
    /// The bound store (for report building); null until enable() succeeded.
    ExperimentStore *store();
    QString runIdForExecution( const QString &executionRef ) const;
    /// Execution refs this recorder has started (insertion order).
    QStringList recordedExecutionRefs() const;

    /// Drains queued coordinator deliveries targeted at this object (shutdown
    /// paths with no running event loop).
    void flush();

  private slots:
    void onRunStateChanged( const QString &runId, const QString &workflowId,
                            const QString &state, qint64 startedMs, qint64 finishedMs );

  private:
    QString checkpointPathFor( const QString &runId ) const;

    workflow::WorkflowRunCoordinator &m_coordinator;
    ExperimentStore m_store;
    std::unique_ptr<ExperimentRunBridge> m_bridge;
    std::unique_ptr<sicnu::dataset::DatasetStore> m_datasetStore;
    OperationTrailSource m_trailSource;
    /// Executions this recorder saw start — the only ones it may close.
    QSet<QString> m_trackedRefs;
    QStringList m_trackedOrder;
    QString m_experimentDbPath;
    bool m_enabled = false;
    bool m_connected = false;
};

} // namespace sicnu::experiment
