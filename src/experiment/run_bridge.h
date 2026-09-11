// run_bridge.h — Workflow/TaskCenter → ExperimentRun auto-recording bridge
// (goal 8.0 §A). The missing half of the 7.0 recorder: this is the state
// machine that turns a stream of authoritative execution lifecycle events
// into ExperimentStore records through ExperimentRunRecorder.
//
// Layering contract: this unit lives in sicnu_experiment and knows NOTHING
// about the workflow layer. The execution side is expressed as a neutral
// ExecutionEvent with a CLOSED state vocabulary; an adapter (see
// src/experiment/bridge/) converts WorkflowRun aggregates to events. The
// bridge EXECUTES nothing (no second scheduler) — it records what the
// authoritative execution plane reports, truthfully:
//   - success is only ever recorded when the execution plane says Completed;
//   - Failed/Cancelled/Interrupted are recorded as such, never swallowed;
//   - unknown state strings are refused, never guessed;
//   - identity pins are only what an explicit registry supplied — an
//     auto-recorded run never fabricates dataset/split/model identity.
#pragma once

#include "experiment_store.h"
#include "experiment_types.h"
#include "run_recorder.h"

#include "../data/data_result.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{

/// The execution states a bridge may be told about. Deliberately a closed
/// string vocabulary: it mirrors the workflow layer's terminal states without
/// a type-level dependency, and anything outside the set is an error.
/// "Canceled" (one L) matches the workflow layer's spelling.
inline const QStringList &bridgeExecutionStates()
{
    static const QStringList states{
        QStringLiteral( "Running" ),   QStringLiteral( "Completed" ),
        QStringLiteral( "Failed" ),    QStringLiteral( "Canceled" ),
        QStringLiteral( "Interrupted" ),
    };
    return states;
}

/// One lifecycle event reported by the execution plane. `definition`,
/// `steps` and `artifacts` are EVIDENCE (stored inside the run's metrics
/// document under "workflow") — they describe what ran, they are never
/// interpreted as instructions.
struct ExecutionEvent
{
    QString executionRef;  ///< authoritative execution id (workflow run id)
    QString workflowId;
    QString state;         ///< member of bridgeExecutionStates()
    qint64 startedMs = 0;
    qint64 finishedMs = 0;
    QJsonObject definition; ///< workflow definition snapshot (bounded)
    QJsonArray steps;       ///< per-step summaries: id/operator/status/error/output digest
    QJsonObject artifacts;  ///< stepId → {path, size, digest?}
    QString errorMessage;
    QJsonObject extra;      ///< additional caller evidence (secret-redacted before persist)

    bool isValid() const { return !executionRef.isEmpty(); }
};

/// Identity pins for auto-recorded runs. Only what a caller explicitly
/// supplied lands in the record; empty fields stay empty (the recorder
/// verifies dataset pins against the store when one is wired).
struct RunPins
{
    QString datasetVersionId;
    QString splitManifestId;
    QString modelId;
    QString modelDigest;
    quint64 seed = 0;
    bool hasSeed = false;

    QJsonObject toJson() const;
    static Result<RunPins> fromJson( const QJsonObject &json );
    bool isEmpty() const;
};

/// Evidence about a stored execution checkpoint, used to decide stale
/// reconciliations. Supplied by the adapter layer (which can read
/// checkpoints and probe cross-process run locks); the bridge core stays
/// workflow-free.
struct ExecutionEvidence
{
    QString executionRef;
    QString state;         ///< checkpoint state string ("Failed", "Canceled", …)
    bool present = false;  ///< false = no checkpoint evidence available at all
    bool liveOwner = false; ///< another LIVE process owns the execution — the
                            ///  record is skipped, never closed behind the
                            ///  owner's back
};

class ExperimentRunBridge
{
  public:
    /// The bridge records into @p store through its own recorder.
    explicit ExperimentRunBridge( ExperimentStore &store );

    /// Optional dataset store for dataset pin verification/fingerprint fill.
    void setDatasetStore( const sicnu::dataset::DatasetStore *store );

    /// Idempotently creates (or verifies) the target experiment. Every
    /// recorded run lands in it.
    Result<void> ensureExperiment( const QString &experimentId, const QString &name,
                                   const QString &objective = QString() );
    void setTargetExperiment( const QString &experimentId )
    {
        QMutexLocker lock( &m_mutex );
        m_experimentId = experimentId;
    }
    QString targetExperiment() const
    {
        QMutexLocker lock( &m_mutex );
        return m_experimentId;
    }

    /// Pre-submission pin registry keyed by workflow id. MUST be set before
    /// the Running event for the pins to take effect (identity pins are
    /// immutable once a run started — the store enforces this).
    void setWorkflowPins( const QString &workflowId, const RunPins &pins );
    /// Per-execution override (wins over the workflow pins). Effective only
    /// before the run started; attaching identity-changing pins afterwards
    /// fails with `experiment.bridge_pins_late`.
    Result<void> attachExecutionPins( const QString &executionRef, const RunPins &pins );

    /// Resolves pins for an execution (execution overrides ⊕ workflow defaults).
    /// Thread-safe snapshot; the per-event path uses the locked-private
    /// variant instead (the event handler already holds the mutex).
    RunPins pinsForExecution( const QString &executionRef, const QString &workflowId ) const;

    /// Advances the target experiment's records by one lifecycle event.
    /// Returns the run id (empty when the event was a tolerated no-op —
    /// e.g. duplicate delivery of an already-recorded terminal state).
    /// Typed failures include:
    ///   experiment.bridge_no_target        — no target experiment set
    ///   experiment.bridge_invalid_event    — empty ref / unknown state
    ///   experiment.bridge_unknown_execution — terminal event for an
    ///       execution this bridge never saw started (backfilling a start
    ///       would fabricate history)
    ///   experiment.bridge_pins_late        — identity pins after start
    ///   experiment.*                       — recorder/store failures
    Result<QString> handleExecutionEvent( const ExecutionEvent &event );

    /// Stale-run reconciliation with the decision policy applied: runs whose
    /// execution is live are untouched; dead executions are closed according
    /// to their checkpoint evidence (Failed→Failed, Canceled→Cancelled,
    /// Interrupted→Interrupted); absent evidence is REPORTED, never guessed.
    /// @p checkpointLookup may return std::nullopt when it cannot answer.
    struct StaleDecision
    {
        QString runId;
        QString executionRef;
        QString action;  // "failed" | "cancelled" | "interrupted" | "report"
                         // | "live" (owned by a live process — untouched)
        QString detail;
    };
    using CheckpointLookup = std::function<std::optional<ExecutionEvidence>( const QString &executionRef )>;
    QVector<StaleDecision> reconcileStale( const QSet<QString> &liveExecutionRefs,
                                           const CheckpointLookup &checkpointLookup );

    /// Live in-session mapping (falls back to a bounded store scan when the
    /// bridge was constructed after the run was recorded).
    QString runIdForExecution( const QString &executionRef ) const;

  private:
    Result<QString> startFromEvent( const ExecutionEvent &event, const RunPins &pins );
    QString resolveRunId( const QString &executionRef ) const;
    static QJsonObject workflowEvidence( const ExecutionEvent &event );
    RunPins pinsForExecutionLocked( const QString &executionRef,
                                    const QString &workflowId ) const;

    ExperimentStore *m_store = nullptr;
    ExperimentRunRecorder m_recorder;
    const sicnu::dataset::DatasetStore *m_datasetStore = nullptr;
    QString m_experimentId;
    QHash<QString, RunPins> m_pinsByWorkflow;
    QHash<QString, RunPins> m_pinsByExecution;
    mutable QHash<QString, QString> m_runIdByExecution;
    /// The bridge serializes event delivery (queued signals) against direct
    /// pin-attachment calls from other threads.
    mutable QMutex m_mutex;
};

} // namespace sicnu::experiment
