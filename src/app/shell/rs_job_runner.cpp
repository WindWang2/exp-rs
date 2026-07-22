/***************************************************************************
 * rs_job_runner.cpp
 ***************************************************************************/
#include "rs_job_runner.h"

#include "job_engine_qt_bridge.h"

#include <QMetaObject>

using sicnu::jobs::JobEngine;
using sicnu::jobs::JobRecord;
using sicnu::jobs::JobState;

namespace {

QString watchJob( const std::string &id,
                  RsJobRunner::FinishedFn onFinished,
                  QObject *context )
{
  if ( id.empty() || !onFinished )
    return {};

  const QString jobId = QString::fromStdString( id );
  auto *bridge = JobEngineQtBridge::instance();

  // Lifetime: parent a tiny holder under context (or bridge) so closing a
  // window drops the connection before the callback fires.
  QObject *lifetime = context ? context : static_cast<QObject *>( bridge );
  auto *holder = new QObject( lifetime );
  auto *conn = new QMetaObject::Connection;

  *conn = QObject::connect(
    bridge, &JobEngineQtBridge::jobFinished, holder,
    [jobId, onFinished, conn, holder]( const QString &finishedId ) {
      if ( finishedId != jobId )
        return;
      QObject::disconnect( *conn );
      delete conn;

      RsJobFinish fin;
      fin.jobId = jobId;
      if ( auto snap = JobEngine::instance().snapshot( jobId.toStdString() ) )
      {
        fin.state = snap->state;
        fin.record = *snap;
        fin.result = snap->result;
        fin.error = QString::fromStdString( snap->error );
      }
      else
      {
        fin.state = JobState::Failed;
        fin.error = QObject::tr( "任务记录丢失" );
      }

      onFinished( fin );
      holder->deleteLater();
    },
    Qt::QueuedConnection );

  return jobId;
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

  const std::string id = JobEngine::instance().submit(
    std::move( req ), std::move( body ), std::move( onCancel ) );
  return watchJob( id, std::move( onFinished ), context );
}

QString RsJobRunner::runOperator( sicnu::jobs::JobRequest req,
                                  FinishedFn onFinished,
                                  QObject *context )
{
  if ( !onFinished )
    return {};

  const std::string id = JobEngine::instance().submit( std::move( req ) );
  return watchJob( id, std::move( onFinished ), context );
}
