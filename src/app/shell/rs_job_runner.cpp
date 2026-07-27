/***************************************************************************
 * rs_job_runner.cpp — routes through Task Center (no direct JobEngine submit)
 ***************************************************************************/
#include "rs_job_runner.h"

#include <QMetaObject>

using sicnu::TaskStatus;
using sicnu::jobs::JobState;

namespace {

sicnu::jobs::JobState mapStatus( TaskStatus status )
{
  switch ( status )
  {
    case TaskStatus::Queued:
      return JobState::Queued;
    case TaskStatus::Running:
    case TaskStatus::Paused:
      return JobState::Running;
    case TaskStatus::Completed:
      return JobState::Succeeded;
    case TaskStatus::Canceled:
      return JobState::Cancelled;
    case TaskStatus::Failed:
    default:
      return JobState::Failed;
  }
}

QString watchTask( long taskId,
                   RsJobRunner::FinishedFn onFinished,
                   QObject *context )
{
  if ( taskId <= 0 || !onFinished )
    return {};

  auto *lifetime = context ? context
                           : static_cast<QObject *>(
                               const_cast<sicnu::TaskCenter *>( &sicnu::TaskCenter::instance() ) );
  auto *holder = new QObject( lifetime );
  auto *conn = new QMetaObject::Connection;

  *conn = QObject::connect(
    &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated, holder,
    [taskId, onFinished, conn, holder]( const sicnu::AlgorithmTaskInfo &info ) {
      if ( info.taskId != taskId )
        return;
      if ( info.status != TaskStatus::Completed
           && info.status != TaskStatus::Failed
           && info.status != TaskStatus::Canceled )
        return;

      QObject::disconnect( *conn );
      delete conn;

      RsJobFinish fin;
      fin.taskId = taskId;
      fin.jobId = QString::fromStdString( info.jobId );
      fin.state = mapStatus( info.status );
      fin.result = info.resultPayload;
      fin.error = info.errorMessage;
      onFinished( fin );
      holder->deleteLater();
    },
    Qt::QueuedConnection );

  return QString::number( taskId );
}

} // namespace

QString RsJobRunner::run( sicnu::jobs::JobRequest req,
                          Body body,
                          FinishedFn onFinished,
                          CancelHook onCancel,
                          QObject *context )
{
  if ( !onFinished )
    return {};

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req, std::move( body ), std::move( onCancel ) );
  return watchTask( taskId, std::move( onFinished ), context );
}

QString RsJobRunner::runOperator( sicnu::jobs::JobRequest req,
                                  FinishedFn onFinished,
                                  QObject *context )
{
  if ( !onFinished )
    return {};

  const long taskId = sicnu::TaskCenter::instance().submitJob( std::move( req ) );
  return watchTask( taskId, std::move( onFinished ), context );
}
