// src/app/shell/gui_job_adapter.h — Unified GUI job tracking and cancellation adapter
#pragma once

#include "processing/framework/task_center.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

namespace sicnu::app {

/**
 * GuiJobHandle encapsulates task lifecycle tracking, terminal status filtering,
 * result payload parsing, cancellation, and busy-gating for GUI dialogs and windows.
 */
class GuiJobHandle : public QObject {
  Q_OBJECT
public:
  using SuccessCallback = std::function<void( const QString &outputPath, const Json::Value &resultPayload )>;
  using FailureCallback = std::function<void( const QString &errorMessage, bool wasCanceled )>;
  using ProgressCallback = std::function<void( int percent, const QString &statusText )>;

  explicit GuiJobHandle( QObject *parent = nullptr, sicnu::TaskCenter *taskCenter = nullptr );
  ~GuiJobHandle() override;

  /// Returns true if a task is currently pending or running on this handle.
  bool isRunning() const { return m_taskId >= 0; }

  /// Returns the current active task ID, or -1 if none.
  long taskId() const { return m_taskId; }

  /// Submits a JobRequest via TaskCenter and sets up tracking callbacks.
  /// Returns submitted task ID (> 0) on success, or -1 if busy or rejected.
  long submitJob( const sicnu::jobs::JobRequest &req,
                  SuccessCallback onSuccess,
                  FailureCallback onFailure,
                  ProgressCallback onProgress = nullptr );

  /// Submits a JobRequest with a custom JobExecutor and cancel callback via TaskCenter.
  long submitJob( const sicnu::jobs::JobRequest &req,
                  sicnu::JobExecutor executor,
                  std::function<void()> cancelCallback,
                  bool autoLoad,
                  SuccessCallback onSuccess,
                  FailureCallback onFailure,
                  ProgressCallback onProgress = nullptr );

  /// Submits an enqueueTask request via TaskCenter and sets up tracking callbacks.
  /// Returns submitted task ID (> 0) on success, or -1 if busy or rejected.
  long submitTask( const QString &algorithmId,
                   const QVariantMap &params,
                   bool autoLoad,
                   SuccessCallback onSuccess,
                   FailureCallback onFailure,
                   ProgressCallback onProgress = nullptr );

  /// Cancels the currently pending task if active.
  void cancel();

signals:
  void taskCompleted( const QString &outputPath, const Json::Value &resultPayload );
  void taskFailed( const QString &errorMessage, bool wasCanceled );
  void taskProgress( int percent, const QString &statusText );

private slots:
  void onTaskUpdated( const sicnu::AlgorithmTaskInfo &info );

private:
  sicnu::TaskCenter *m_taskCenter = nullptr;
  long m_taskId = -1;
  SuccessCallback m_onSuccess;
  FailureCallback m_onFailure;
  ProgressCallback m_onProgress;
};

} // namespace sicnu::app
