/***************************************************************************
 * rs_job_runner.h  —  thin UI helper over Task Center
 *
 * One small interface: run(request, body, onFinished [, onCancel, context]).
 * Submits through TaskCenter (JobEngine remains internal). Prefer calling
 * TaskCenter::submitJob directly from new code; this helper only exists for
 * optional finish-callback convenience and must not bypass Task Center.
 ***************************************************************************/
#pragma once

#include "jobs/job_types.h"
#include "processing/framework/task_center.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

/**
 * Result delivered on the GUI thread when a task reaches a terminal state.
 */
struct RsJobFinish
{
  QString jobId;   ///< Underlying JobEngine id when present
  long taskId = -1;
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
 * thread via TaskCenter::taskUpdated.
 */
class RsJobRunner
{
  public:
    using Body = sicnu::TaskCenter::JobExecutor;
    using CancelHook = sicnu::TaskCenter::CancelHook;
    using FinishedFn = std::function<void( const RsJobFinish & )>;

    /**
     * Submit \a req with \a body (and optional \a onCancel) via Task Center.
     * When the task finishes, \a onFinished is invoked once on the GUI thread.
     *
     * \a context: if non-null, the finish connection is owned by \a context
     * and auto-drops if the context is destroyed (avoids use-after-free).
     *
     * \return Task Center task id as string (empty on submit failure).
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
