#include "output_committer.h"

#include "framework/task_center.h"

using namespace sicnu::data;

namespace sicnu
{

namespace
{

Diagnostic taskCommitDiagnostic( const QString &code, const QString &message )
{
  return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

} // namespace

CommitResult OutputCommitter::commitTaskOutput( TaskCenter *taskCenter,
                                                long taskId,
                                                AssetKind kind,
                                                const QString &stablePath,
                                                PersistencePolicy persistence,
                                                bool autoLoad,
                                                const DerivationRecord &derivation )
{
  Q_ASSERT( taskCenter != nullptr );
  const AlgorithmTaskInfo task = taskCenter->getTaskInfo( taskId );

  // Only a successfully completed task may register an output. A failed or
  // cancelled task performs no publish, no registration, and no Derivation
  // Record — its caller discards the temporary output instead.
  if ( task.status != TaskStatus::Completed )
  {
    return CommitResult::failure( taskCommitDiagnostic(
      QStringLiteral( "output.task_not_completed" ),
      QStringLiteral( "Task %1 is not completed (status=%2); refusing to commit "
                      "an output from an incomplete task" )
        .arg( QString::number( taskId ) ) ) );
  }

  if ( task.outputLayerPath.isEmpty() )
  {
    return CommitResult::failure( taskCommitDiagnostic(
      QStringLiteral( "output.no_output" ),
      QStringLiteral( "Task %1 completed without an OUTPUT-keyed result path" )
        .arg( QString::number( taskId ) ) ) );
  }

  AlgorithmOutputRequest request;
  request.kind = kind;
  request.tempPath = task.outputLayerPath;
  request.stablePath = stablePath;
  request.persistence = persistence;
  request.autoLoad = autoLoad;
  request.derivation = derivation;
  request.derivation.taskReference = QString::number( taskId );
  return commit( request );
}

} // namespace sicnu
