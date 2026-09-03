#pragma once

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

#include "workflow_run.h"

namespace sicnu::workflow {

class WorkflowCheckpointManager {
public:
  WorkflowCheckpointManager() = default;

  /// Atomically save the workflow run checkpoint to disk. The payload is
  /// written to a per-save unique .tmp file, fsync'd, then moved onto the final
  /// .json path with an atomic replace (POSIX rename / MoveFileEx), so a crash
  /// at any point leaves either the previous or the new checkpoint intact -
  /// never a missing or half-written one. Write/flush failures abort the save
  /// (returning an empty string) and leave the previous checkpoint untouched.
  /// If directoryPath is empty, uses defaultCheckpointDirectory().
  QString saveCheckpoint( const WorkflowRun &run, const QString &directoryPath = QString() );

  /// Load a workflow run checkpoint from file. Rejects payloads with a
  /// missing/unsupported serialization version, unsafe runId, unknown state
  /// strings, or invalid step-plan enum values (reported via @a error).
  std::unique_ptr<WorkflowRun> loadCheckpoint( const QString &filePath, QString *error = nullptr );

  /// List all checkpoint files (*.json) in the directory.
  QStringList listCheckpoints( const QString &directoryPath = QString() );

  /// Recover interrupted runs: transitions any active non-terminal run (Running, Planning, WaitingResource, Cancelling)
  /// to Interrupted state, resets step plans stuck in Running/Cancelling back
  /// to Pending, and saves the updated checkpoint back to disk. Ownership-aware
  /// (#727): a run whose lock is held by a live process is skipped untouched
  /// (never reconciled, never reported); acquiring a run's lock proves its
  /// previous owner is gone. Runs whose re-save fails are not reported as
  /// recovered (disk state is unchanged, so the next recovery pass retries
  /// them). Orphaned .tmp files from crashed saves are swept. Corrupt/
  /// unreadable checkpoints are skipped with a warning.
  std::vector<std::shared_ptr<WorkflowRun>> recoverInterruptedRuns( const QString &directoryPath = QString() );

  /// In-memory reconciliation shared by recoverInterruptedRuns and resumeRun
  /// (#727): force-sets an active state (Running/Planning/WaitingResource/
  /// Cancelling) to Interrupted and resets steps stuck in Running/Cancelling
  /// to Pending. Returns false when @a run is not in an active state (nothing
  /// to reconcile). Callers are responsible for persisting afterwards.
  static bool reconcileToInterrupted( WorkflowRun &run );

  /// Default directory for workflow checkpoints.
  static QString defaultCheckpointDirectory();
};

} // namespace sicnu::workflow
