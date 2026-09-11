// run_recorder.h — TaskCenter/Workflow → ExperimentRun adapter (goal 7.0 §C).
//
// The recorder is the authoritative bridge between an execution performed
// through the existing Workflow/TaskCenter/JobEngine seams and the
// ExperimentStore record of it. It EXECUTES nothing (no second scheduler):
// callers — workflow state hooks, pipeline runners, tests — call it at the
// existing lifecycle points.
//
// Truthful-state contract (ADR 0130): failure and cancellation are recorded
// as such, never swallowed into a success. A crash that leaves a run
// non-terminal is surfaced by reconcileStaleRuns() — a read-only report;
// deciding the outcome (failed vs cancelled) belongs to the caller, the
// recorder never auto-closes a run.
//
// Environment capture goes through RunEnvironment (allowlist + secret
// denylist); the recorder re-applies redacted() before persisting so an
// assembled-before-filtering request cannot leak through (#789 defense).
#pragma once

#include "evaluation.h"
#include "experiment_store.h"
#include "experiment_types.h"

#include <QSet>

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{

using sicnu::dataset::DeterminismGrade;
using sicnu::dataset::Diagnostic;
using sicnu::dataset::Result;
using sicnu::dataset::RunStatus;

/// Everything needed to OPEN a run record. The identity pins mirror
/// ExperimentRun 1:1; `executionRef` is the back pointer to the platform
/// execution (workflow run id / task id).
struct RunStartRequest
{
    QString experimentId;
    QString algorithmId;
    QString algorithmVersion;
    QJsonObject parameters; ///< canonicalized on hashing, stored as given
    QString datasetVersionId;
    /// Empty + datasetStore wired ⇒ resolved from the store (and verified to
    /// exist); the version's committed fingerprint is stamped.
    QString datasetFingerprint;
    QString splitManifestId;
    QString splitFingerprint;
    QString modelId;
    QString modelDigest;
    quint64 seed = 0;
    DeterminismGrade determinism = DeterminismGrade::Strict;
    /// REQUIRED when determinism != Strict (goal §30).
    QString determinismNote;
    QString softwareRevision;
    QString executionRef;
    RunEnvironment environment; ///< default-constructed = captureCurrent()
};

class ExperimentRunRecorder
{
  public:
    explicit ExperimentRunRecorder( ExperimentStore &store );

    /// Optional dataset store used to resolve/verify dataset pins in
    /// startRun (fingerprint auto-fill + version existence check).
    void setDatasetStore( const sicnu::dataset::DatasetStore *store );

    /// Creates the run record with status Created, stamps identity hashes
    /// and the (redacted) environment, then advances it to Running. Returns
    /// the generated run id. Typed failures:
    ///   experiment.recorder_missing_experiment — experimentId unknown
    ///   experiment.recorder_missing_version    — dataset version unknown
    ///   experiment.bad_transition              — store transition refusal
    Result<QString> startRun( const RunStartRequest &request );

    /// Terminal success: artifacts + metrics land in the SAME transition as
    /// the status change (content rides the store's checked transaction).
    Result<void> markSucceeded( const QString &runId, const QVector<ExperimentRun::Artifact> &artifacts,
                                const QJsonObject &metrics );

    /// Truthful failure. The error evidence is stored in the run's metrics
    /// document under "error" so a failed run explains itself.
    Result<void> markFailed( const QString &runId, const QString &errorCode,
                             const QString &message );

    /// Truthful cancellation. The reason is stored under "cancel_reason".
    Result<void> markCancelled( const QString &runId, const QString &reason );

    /// Truthful interruption (crash / startup recovery / lost execution).
    /// NON-terminal by contract: a resumed execution re-enters Running via
    /// markResumed on the SAME run id. The note is stored under
    /// "interrupt_note". Re-delivering Interrupted for an already-interrupted
    /// run is an idempotent success (queued event sources may repeat).
    Result<void> markInterrupted( const QString &runId, const QString &note );

    /// Interrupted → Running: a resumed execution continues the SAME record
    /// (the workflow layer swaps resumed submissions back under the original
    /// execution ref). Typed failures: run_not_found / bad_transition when
    /// the run is not Interrupted.
    Result<void> markResumed( const QString &runId );

    /// Records the evaluation protocol + metrics for a run (MetricRecord).
    Result<void> recordMetrics( const QString &runId, const EvaluationProtocol &protocol,
                                const QJsonObject &metrics );

    /// One stale run: created/running but its execution is not live.
    struct StaleRun
    {
        QString runId;
        RunStatus status = RunStatus::Created;
        QString executionRef;
        QDateTime startedAtUtc;
    };

    /// Crash truth report: non-terminal runs whose executionRef is absent
    /// from @p liveExecutionRefs (an empty ref counts as not-live). Runs
    /// with no executionRef at all are still listed — an unlinked run is
    /// exactly the case a crash leaves behind. READ-ONLY: the caller decides
    /// and calls markFailed/markCancelled explicitly.
    QVector<StaleRun> reconcileStaleRuns( const QSet<QString> &liveExecutionRefs ) const;

  private:
    Result<ExperimentRun> loadRun( const QString &runId ) const;

    ExperimentStore *m_store = nullptr;
    const sicnu::dataset::DatasetStore *m_datasetStore = nullptr;
};

} // namespace sicnu::experiment
