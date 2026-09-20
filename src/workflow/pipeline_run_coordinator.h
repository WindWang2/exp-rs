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

#include <QHash>
#include <QObject>
#include <QString>
#include <QThreadPool>

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
    QString artifactFingerprint; // "sha256fl:<hex>" — see coordinator cpp
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
/// writes artifacts.
using NodeExecutor = std::function<NodeExecutionResult(
    const NodeFact &node, const QHash<QString, QString> &inputArtifacts, const QString &runDirectory )>;

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

    /// The checkpoint file this run persists to (empty when idle).
    QString checkpointPath() const;

    signals:
    void nodeStatusChanged( const QString &nodeId, sicnu::workflow::ExecutionState state, float progress );
    void nodeFinished( const QString &nodeId, bool success, const QString &artifactPath );
    void pipelineCompleted( bool success, const QString &summary );
    void checkpointPersisted( const QString &checkpointFilePath );

  private:
    struct RunState;

    void dispatchReadyNodes();
    void onNodeFinished( const QString &nodeId, NodeExecutionResult result, qint64 elapsedMs );
    void finalizeIfDone();
    void persistCheckpoint();
    void markRemaining( ExecutionState state );

    std::unique_ptr<RunState> m_state;
};

} // namespace sicnu::workflow

Q_DECLARE_METATYPE( sicnu::workflow::ExecutionState )
