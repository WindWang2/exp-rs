// src/workflow/pipeline_run_coordinator.h — tier-scheduled, checkpointed run engine (D17, ADR 0162)
#pragma once

//
// Instance-based workflow run coordinator for the D17 designer stack (the
// production TaskCenter bridge keeps the WorkflowRunCoordinator name; see
// DECISIONS D1/D3).
//
// Scheduling: the Kahn frontier. A node is dispatched iff every parent is
// Succeeded; any Failed/Cancelled/Skipped parent marks the node Skipped
// (cascade). Work runs on a private QThreadPool sized by setMaxParallelism
// (clamped to [1, 8]); workers never block on other nodes (issue #798
// discipline) — completion is event-driven back to the coordinator thread.
//
// Checkpoints: after every node terminal transition, the full status
// snapshot is written atomically — JSON to `checkpoint_<runId>.json.tmp`,
// flush, fsync (POSIX) / _commit (Win32), rename over the target, directory
// fsync. The envelope is gated on load: `kind` must be the D17 tag and
// `version` must be a member of the closed supported set — a checkpoint this
// build did not write is refused, never reinterpreted.
//
// resumeFromCheckpoint replays the document: a node is CacheHit iff its
// recomputed lineage signature matches the recorded one AND its recorded
// artifact still verifies — exists, resolves (symlinks included) inside the
// recorded run directory, and matches the recorded size / modification time /
// content fingerprint. A tampered, moved, or foreign artifact is never
// served: the node reverts to Pending and recomputes. Executors returning a
// path outside the run directory fail their node with
// `ir2.artifact_outside_run:` instead of recording a false success.
//
// Node work is an injectable NodeExecutor — production may bind real
// operators, tests bind deterministic synthetic ones.
//
// Threading contract (Track 13): every public entry point marshals itself
// onto the coordinator's affinity thread (Qt::BlockingQueuedConnection; the
// call runs inline when the caller already lives there). The affinity thread
// never blocks on a foreign thread, so no call can deadlock against it; a
// foreign caller may block until the owner is idle, bounded by the owner's
// current unit of work. requestCancel is two-phase so a canceller never waits
// on a whole-file hash. Destroy the coordinator on its affinity thread (the
// fast path) — a foreign-thread destruction is safe but marshals a drain,
// which completes only while that thread services its event loop.
//
// Cross-process ownership (#727 parity): a started or resumed run holds a
// WorkflowRunLock next to its checkpoint for the whole execution — a second
// process (or coordinator) resuming the same checkpoint mid-flight is
// refused with a typed error instead of double-executing the remaining
// nodes. The lock releases when the run finalizes; a terminal checkpoint can
// then be re-verified (all-CacheHit) by anyone.
//

#include <QHash>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <functional>

#include "workflow/workflow_ir_v2.h"
#include "workflow/sicnu_workflow_export.h"

namespace sicnu::workflow {

enum class ExecutionState
{
    Pending,
    Ready,
    Running,
    Succeeded,
    Failed,
    Cancelled,
    Skipped
};

QString executionStateString( ExecutionState state ); // "Pending", ...

/// Artifact identity strength for node successes (Track 13, DECISIONS D2).
/// The recorded tag is self-describing ("sha256fl:" / "sha256full:"), so the
/// resume verifier always re-derives with the algorithm that stamped it —
/// legacy checkpoints keep fast verification and never need migration.
enum class ArtifactIdentityMode
{
    /// sha256fl: [8B LE size][first ≤1MiB][last ≤1MiB] — whole file when
    /// ≤ 2 MiB. O(2 MiB) per artifact; cannot see a mid-file rewrite of a
    /// larger artifact.
    Fast,
    /// sha256full: same framing, then the WHOLE file streamed in bounded
    /// chunks with a cancel poll — closes the mid-file blind spot for
    /// arbitrarily large artifacts.
    Full,
    /// Full exactly when the artifact is larger than the fast window (i.e.
    /// when Fast would have a blind spot), Fast below. Default.
    Auto
};

struct NodeStatusSnapshot
{
    QString nodeId;
    ExecutionState state = ExecutionState::Pending;
    float progress = 0.0f;
    QString errorMessage;
    qint64 elapsedMs = 0;
    QString outputArtifactPath;
    /// Artifact identity recorded at success time and re-verified on resume
    /// (DECISIONS D3): -1 / empty when no artifact was produced or the run
    /// predates checkpoint format 1.1 (which forces a conservative recompute).
    qint64 artifactSizeBytes = -1;
    qint64 artifactLastModifiedMs = -1;
    /// "sha256fl:<hex>" (legacy scheme) or "sha256full:<hex>" (whole-file,
    /// Track 13). Empty when no artifact was produced, the run predates
    /// checkpoint format 1.1 (which forces a conservative recompute), or the
    /// node was cancelled while its identity was being computed.
    QString artifactFingerprint;
    bool isCacheHit = false;
    QString lineageSignature;
};

/// Result of executing one node. @p artifactPath is the produced file
/// (recorded in the checkpoint and fed to downstream executors).
struct NodeExecutionResult
{
    bool success = false;
    QString artifactPath;
    QString errorMessage;
};

/// Inputs: IR2 target port name -> artifact path (D-W6; prefer explicit port
/// names over source-node-id keys). The run directory is where the executor
/// writes artifacts. @p cancelRequested is the run's cooperative
/// cancellation flag — the executor wires it into RSOperatorContext so a
/// requestCancel() aborts a long-running registry operator mid-run (#1152).
using NodeExecutor = std::function<NodeExecutionResult(
    const NodeFact &node, const QHash<QString, QString> &inputArtifacts,
    const QString &runDirectory, const std::atomic<bool> *cancelRequested )>;

/// Deterministic synthetic executor (D17 hermetic tests): artifact bytes are
/// a function of the node signature. Bind it explicitly via setExecutor —
/// the coordinator never falls back to it (#1006: a run without a bound
/// executor fails its nodes with ir2.executor_missing instead of silently
/// synthesizing success).
NodeExecutor makeSyntheticNodeExecutor();

class SICNU_WORKFLOW_EXPORT PipelineRunCoordinator : public QObject
{
    Q_OBJECT

  public:
    explicit PipelineRunCoordinator( QObject *parent = nullptr );
    ~PipelineRunCoordinator() override;

    PipelineRunCoordinator( const PipelineRunCoordinator & ) = delete;
    PipelineRunCoordinator &operator=( const PipelineRunCoordinator & ) = delete;

    /// Validates + schedules a run. Fails closed on cyclic documents or an
    /// already-running coordinator. Persists an initial checkpoint.
    bool startRun( const WorkflowDocument &def, const QString &runDirectory, QString *outError = nullptr );

    /// Cooperative cancel: queued nodes -> Cancelled, running nodes drain
    /// and are marked Cancelled; pipelineCompleted(false) follows.
    void requestCancel();

    /// Loads a checkpoint document; Succeeded nodes whose artifact still
    /// exists and whose lineage signature still matches become CacheHit;
    /// the remaining topology resumes. Emits pipelineCompleted at the end.
    bool resumeFromCheckpoint( const QString &checkpointFilePath, QString *outError = nullptr );

    QMap<QString, NodeStatusSnapshot> getAllStatuses() const;
    bool isRunning() const;
    /// True once the current run reached a terminal state (also for empty
    /// documents, which complete synchronously inside startRun/resume).
    bool hasCompleted() const;

    /// Binds the node executor (production: makeRegistryNodeExecutor from
    /// ir2_registry_node_executor.h; hermetic tests: makeSyntheticNodeExecutor).
    /// A run started without a bound executor fails every node — there is no
    /// implicit synthetic fallback (#1006).
    void setExecutor( NodeExecutor executor );
    void setMaxParallelism( int workers );

    /// Artifact identity strength for subsequent node successes (default
    /// Auto). Affects only what NEW successes record; resume verification
    /// follows each recorded tag.
    void setArtifactIdentityMode( ArtifactIdentityMode mode );
    ArtifactIdentityMode artifactIdentityMode() const;

    /// The checkpoint file this run persists to (empty when idle).
    QString checkpointPath() const;

    /// The provenance graph written when the run reached a terminal state
    /// (`provenance_<runId>.json` beside the checkpoint for the first attempt;
    /// `attempt-<N>/provenance_<runId>.json` inside the run directory for
    /// later attempts — the lineage lives in a path segment so a user-named
    /// runId can never collide with it). Empty when the run has not finalized
    /// or the write failed — provenance is audit output, it must never fail
    /// the run it describes).
    QString provenancePath() const;

    signals:
    void nodeStatusChanged( const QString &nodeId, sicnu::workflow::ExecutionState state, float progress );
    void nodeFinished( const QString &nodeId, bool success, const QString &artifactPath );
    void pipelineCompleted( bool success, const QString &summary );
    void checkpointPersisted( const QString &checkpointFilePath );

  private:
    struct RunState;

    /// Affinity-thread bodies behind the public marshalling entry points
    /// (startRun / resumeFromCheckpoint). Callers reach them only through the
    /// public methods, which guarantee they run on the owner thread.
    bool startRunOnAffinity( const WorkflowDocument &def, const QString &runDirectory, QString *outError );
    bool resumeOnAffinity( const QString &checkpointFilePath, QString *outError );
    void requestCancelOnAffinity();

    void dispatchReadyNodes();
    void onNodeFinished( const QString &nodeId, NodeExecutionResult result, qint64 elapsedMs );
    void finalizeIfDone();
    void persistCheckpoint();
    void markRemaining( ExecutionState state );

    std::unique_ptr<RunState> m_state;
};

} // namespace sicnu::workflow

Q_DECLARE_METATYPE( sicnu::workflow::ExecutionState )
