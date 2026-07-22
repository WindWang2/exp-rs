/***************************************************************************
 * rs_job_runner.h  —  deep UI adapter over JobEngine + Qt bridge
 *
 * One small interface: run(request, body, onFinished [, onCancel, context]).
 * Callers stop re-copying submit + JobEngineQtBridge + Connection glue.
 ***************************************************************************/
#pragma once

#include "jobs/job_engine.h"
#include "jobs/job_types.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

/**
 * Result delivered on the GUI thread when a job reaches a terminal state.
 */
struct RsJobFinish
{
  QString jobId;
  sicnu::jobs::JobState state = sicnu::jobs::JobState::Failed;
  std::optional<sicnu::jobs::JobRecord> record;
  Json::Value result{ Json::objectValue };
  QString error;

  bool succeeded() const { return state == sicnu::jobs::JobState::Succeeded; }
  bool cancelled() const { return state == sicnu::jobs::JobState::Cancelled; }
  bool failed() const
  {
    return state == sicnu::jobs::JobState::Failed
           || ( !succeeded() && !cancelled() );
  }
};

/**
 * Stateless helper (not a QObject). Safe to call from the GUI thread only
 * for the run() entry; the finish callback is always marshalled to the GUI
 * thread via JobEngineQtBridge.
 */
class RsJobRunner
{
  public:
    using Body = sicnu::jobs::JobEngine::JobExecutor;
    using CancelHook = sicnu::jobs::JobEngine::CancelHook;
    using FinishedFn = std::function<void( const RsJobFinish & )>;

    /**
     * Submit \a req with \a body (and optional \a onCancel).
     * When the job finishes, \a onFinished is invoked once on the GUI thread.
     *
     * \a context: if non-null, the finish connection is owned by \a context
     * and auto-drops if the context is destroyed (avoids use-after-free).
     *
     * \return job id (empty on submit failure — rare).
     */
    static QString run( sicnu::jobs::JobRequest req,
                        Body body,
                        FinishedFn onFinished,
                        CancelHook onCancel = {},
                        QObject *context = nullptr );

    /**
     * Submit \a req via RSOperator / prefix registry (no per-job body).
     * Same finish marshalling as \ref run.
     */
    static QString runOperator( sicnu::jobs::JobRequest req,
                                FinishedFn onFinished,
                                QObject *context = nullptr );
};
