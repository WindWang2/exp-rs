#pragma once
#include "framework/output_committer.h"
#include "framework/task_center.h"

namespace sicnu
{

inline CommitResult commitTaskOutput( OutputCommitter *committer,
                                      TaskCenter *taskCenter,
                                      long taskId,
                                      data::AssetKind kind,
                                      const QString &stablePath,
                                      data::PersistencePolicy persistence,
                                      bool autoLoad,
                                      const data::DerivationRecord &derivation )
{
  Q_ASSERT( committer != nullptr );
  Q_ASSERT( taskCenter != nullptr );
  const AlgorithmTaskInfo task = taskCenter->getTaskInfo( taskId );

  if ( task.status != TaskStatus::Completed )
  {
    return CommitResult::failure( data::Diagnostic{
      QStringLiteral( "output.task_not_completed" ),
      QStringLiteral( "Task %1 is not completed; refusing to commit an output from an incomplete task" )
        .arg( QString::number( taskId ) ),
      data::DiagnosticSeverity::Error } );
  }

  if ( task.outputLayerPath.isEmpty() )
  {
    return CommitResult::failure( data::Diagnostic{
      QStringLiteral( "output.no_output" ),
      QStringLiteral( "Task %1 completed without an OUTPUT-keyed result path" )
        .arg( QString::number( taskId ) ),
      data::DiagnosticSeverity::Error } );
  }

  AlgorithmOutputRequest request;
  request.kind = kind;
  request.tempPath = task.outputLayerPath;
  request.stablePath = stablePath;
  request.persistence = persistence;
  request.autoLoad = autoLoad;
  request.derivation = derivation;
  request.derivation.taskReference = QString::number( taskId );
  return committer->commit( request );
}

inline CommitResult commitTaskOutput( OutputCommitter &committer,
                                      TaskCenter *taskCenter,
                                      long taskId,
                                      data::AssetKind kind,
                                      const QString &stablePath,
                                      data::PersistencePolicy persistence,
                                      bool autoLoad,
                                      const data::DerivationRecord &derivation )
{
  return commitTaskOutput( &committer, taskCenter, taskId, kind, stablePath, persistence, autoLoad, derivation );
}

} // namespace sicnu
