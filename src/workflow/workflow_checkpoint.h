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

  /// Atomically save the workflow run checkpoint to disk (.tmp -> fsync -> rename .json).
  /// If directoryPath is empty, uses defaultCheckpointDirectory().
  QString saveCheckpoint( const WorkflowRun &run, const QString &directoryPath = QString() );

  /// Load a workflow run checkpoint from file.
  std::unique_ptr<WorkflowRun> loadCheckpoint( const QString &filePath, QString *error = nullptr );

  /// List all checkpoint files (*.json) in the directory.
  QStringList listCheckpoints( const QString &directoryPath = QString() );

  /// Recover interrupted runs: transitions any active non-terminal run (Running, Planning, WaitingResource, Cancelling)
  /// to Interrupted state and saves the updated checkpoint back to disk.
  std::vector<std::shared_ptr<WorkflowRun>> recoverInterruptedRuns( const QString &directoryPath = QString() );

  /// Default directory for workflow checkpoints.
  static QString defaultCheckpointDirectory();
};

} // namespace sicnu::workflow
