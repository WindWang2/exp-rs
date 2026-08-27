// src/app/shell/gui_job_adapter.cpp — Unified GUI job tracking and cancellation adapter
#include "gui_job_adapter.h"

namespace sicnu::app {

GuiJobHandle::GuiJobHandle( QObject *parent, sicnu::TaskCenter *taskCenter )
  : QObject( parent )
  , m_taskCenter( taskCenter ? taskCenter : &sicnu::TaskCenter::instance() )
{
  connect( m_taskCenter, &sicnu::TaskCenter::taskUpdated,
           this, &GuiJobHandle::onTaskUpdated );
}

GuiJobHandle::~GuiJobHandle()
{
  if ( m_taskId >= 0 )
  {
    long idToCancel = m_taskId;
    m_taskId = -1;
    m_onSuccess = nullptr;
    m_onFailure = nullptr;
    m_onProgress = nullptr;
    m_taskCenter->cancelTask( idToCancel );
  }
}

long GuiJobHandle::submitJob( const sicnu::jobs::JobRequest &req,
                              SuccessCallback onSuccess,
                              FailureCallback onFailure,
                              ProgressCallback onProgress )
{
  if ( isRunning() )
    return -1;

  m_onSuccess = std::move( onSuccess );
  m_onFailure = std::move( onFailure );
  m_onProgress = std::move( onProgress );

  const long submittedId = m_taskCenter->submitJob( req );
  m_taskId = submittedId;
  if ( m_taskId < 0 )
  {
    m_onSuccess = nullptr;
    m_onFailure = nullptr;
    m_onProgress = nullptr;
  }
  else
  {
    const auto info = m_taskCenter->getTaskInfo( m_taskId );
    if ( info.status == sicnu::TaskStatus::Completed
         || info.status == sicnu::TaskStatus::Failed
         || info.status == sicnu::TaskStatus::Canceled )
    {
      // Catch-up may already have fired the callbacks and reset m_taskId;
      // the caller still needs the id of the submission that succeeded (#453).
      onTaskUpdated( info );
    }
  }
  return submittedId;
}

long GuiJobHandle::submitJob( const sicnu::jobs::JobRequest &req,
                              sicnu::JobExecutor executor,
                              std::function<void()> cancelCallback,
                              bool autoLoad,
                              SuccessCallback onSuccess,
                              FailureCallback onFailure,
                              ProgressCallback onProgress )
{
  if ( isRunning() )
    return -1;

  m_onSuccess = std::move( onSuccess );
  m_onFailure = std::move( onFailure );
  m_onProgress = std::move( onProgress );

  const long submittedId = m_taskCenter->submitJob( req, std::move( executor ), std::move( cancelCallback ), autoLoad );
  m_taskId = submittedId;
  if ( m_taskId < 0 )
  {
    m_onSuccess = nullptr;
    m_onFailure = nullptr;
    m_onProgress = nullptr;
  }
  else
  {
    const auto info = m_taskCenter->getTaskInfo( m_taskId );
    if ( info.status == sicnu::TaskStatus::Completed
         || info.status == sicnu::TaskStatus::Failed
         || info.status == sicnu::TaskStatus::Canceled )
    {
      // Catch-up may already have fired the callbacks and reset m_taskId;
      // the caller still needs the id of the submission that succeeded (#453).
      onTaskUpdated( info );
    }
  }
  return submittedId;
}

long GuiJobHandle::submitTask( const QString &algorithmId,
                               const QVariantMap &params,
                               bool autoLoad,
                               SuccessCallback onSuccess,
                               FailureCallback onFailure,
                               ProgressCallback onProgress )
{
  if ( isRunning() )
    return -1;

  m_onSuccess = std::move( onSuccess );
  m_onFailure = std::move( onFailure );
  m_onProgress = std::move( onProgress );

  const long submittedId = m_taskCenter->enqueueTask( algorithmId, params, autoLoad );
  m_taskId = submittedId;
  if ( m_taskId < 0 )
  {
    m_onSuccess = nullptr;
    m_onFailure = nullptr;
    m_onProgress = nullptr;
  }
  else
  {
    const auto info = m_taskCenter->getTaskInfo( m_taskId );
    if ( info.status == sicnu::TaskStatus::Completed
         || info.status == sicnu::TaskStatus::Failed
         || info.status == sicnu::TaskStatus::Canceled )
    {
      // Catch-up may already have fired the callbacks and reset m_taskId;
      // the caller still needs the id of the submission that succeeded (#453).
      onTaskUpdated( info );
    }
  }
  return submittedId;
}

void GuiJobHandle::cancel()
{
  if ( m_taskId >= 0 )
  {
    long idToCancel = m_taskId;
    m_taskCenter->cancelTask( idToCancel );
    if ( m_taskId == idToCancel )
    {
      m_taskId = -1;
      auto onFailure = std::move( m_onFailure );
      m_onSuccess = nullptr;
      m_onFailure = nullptr;
      m_onProgress = nullptr;
      if ( onFailure )
      {
        onFailure( QStringLiteral( "Canceled" ), true );
      }
      emit taskFailed( QStringLiteral( "Canceled" ), true );
    }
  }
}

void GuiJobHandle::onTaskUpdated( const sicnu::AlgorithmTaskInfo &info )
{
  if ( m_taskId < 0 || info.taskId != m_taskId )
    return;

  if ( info.status == sicnu::TaskStatus::Running || info.status == sicnu::TaskStatus::Queued
       || info.status == sicnu::TaskStatus::WaitingResource || info.status == sicnu::TaskStatus::Cancelling )
  {
    int pct = static_cast<int>( info.progressPercentage );
    QString statusText = info.errorMessage;
    if ( statusText.isEmpty() && info.status == sicnu::TaskStatus::WaitingResource )
      statusText = QObject::tr( "Waiting for available resources" );
    if ( statusText.isEmpty() && info.status == sicnu::TaskStatus::Cancelling )
      statusText = QObject::tr( "Cancellation in progress" );
    if ( m_onProgress )
    {
      m_onProgress( pct, statusText );
    }
    emit taskProgress( pct, statusText );
    return;
  }

  if ( info.status != sicnu::TaskStatus::Completed
       && info.status != sicnu::TaskStatus::Failed
       && info.status != sicnu::TaskStatus::Canceled )
  {
    return;
  }

  m_taskId = -1;
  auto onSuccess = std::move( m_onSuccess );
  auto onFailure = std::move( m_onFailure );
  m_onSuccess = nullptr;
  m_onFailure = nullptr;
  m_onProgress = nullptr;

  if ( info.status == sicnu::TaskStatus::Completed )
  {
    QString outputPath = info.outputLayerPath;
    if ( outputPath.isEmpty() && info.resultPayload.isMember( "output" ) && info.resultPayload["output"].isString() )
    {
      outputPath = QString::fromStdString( info.resultPayload["output"].asString() );
    }
    if ( outputPath.isEmpty() && info.parameterMap.contains( QStringLiteral( "OUTPUT" ) ) )
    {
      outputPath = info.parameterMap.value( QStringLiteral( "OUTPUT" ) ).toString();
    }

    if ( onSuccess )
    {
      onSuccess( outputPath, info.resultPayload );
    }
    emit taskCompleted( outputPath, info.resultPayload );
  }
  else
  {
    bool wasCanceled = ( info.status == sicnu::TaskStatus::Canceled );
    QString err = wasCanceled ? QStringLiteral( "Canceled" )
                              : ( info.errorMessage.isEmpty() ? QStringLiteral( "Task execution failed" ) : info.errorMessage );
    if ( onFailure )
    {
      onFailure( err, wasCanceled );
    }
    emit taskFailed( err, wasCanceled );
  }
}

} // namespace sicnu::app
