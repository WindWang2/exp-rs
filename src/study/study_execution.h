// study_execution.h — the execution port of the study runner (RS14-07).
//
// The runner NEVER schedules work itself: it holds a bounded in-flight
// submission window over this port, and the production adapter routes every
// point through the existing ExecutionPlane → TaskCenter → JobEngine spine
// (admission, concurrency, RAM budget and lane policy stay with TaskCenter).
// Tests inject a fake backend — no GUI, no QGIS, no network.
//
// Outcome honesty contract: exactly one terminal status per submission; the
// payload is the COMMITTED result (never an uncommitted temporary, #1056);
// failures carry typed "study.*" error codes, never a silent fallback.
#pragma once

#include "data/data_result.h"

#include <QJsonObject>
#include <QString>

#include <chrono>
#include <memory>

namespace sicnu::study
{

using sicnu::data::Result;

struct StudyExecutionOutcome
{
    enum class Status
    {
        Succeeded,
        Failed,    ///< platform/operator failure (errorMessage carries cause)
        Cancelled, ///< execution cancelled (not the study's own cancel path)
        TimedOut,  ///< the study per-run deadline expired (task cancel requested)
    };

    Status status = Status::Failed;
    QJsonObject payload;     ///< committed task result payload (metrics + "output")
    QString errorCode;       ///< typed machine-readable code ("" on success)
    QString errorMessage;    ///< human-readable cause ("" on success)
};

/// One in-flight submission. `executionRef()` is available immediately after
/// submit so the run record can pin the platform execution before waiting.
class StudySubmission
{
  public:
    virtual ~StudySubmission() = default;

    /// Platform execution reference (e.g. the TaskCenter task id as text).
    virtual QString executionRef() const = 0;

    /// Waits for the terminal outcome (event-loop-free contract on the
    /// production adapter). Called once per submission by the runner.
    virtual StudyExecutionOutcome wait( std::chrono::milliseconds timeout ) = 0;

    /// Requests cancellation; idempotent. wait() then reports Cancelled.
    virtual void cancel() = 0;
};

/// The execution backend port. @p pointParameters carries the full parameter
/// document including the runner-assigned "output" path. @p correlationId
/// identifies the point inside the study ("<studyId>/<pointId>#<replicate>").
class IStudyExecutionBackend
{
  public:
    virtual ~IStudyExecutionBackend() = default;

    virtual Result<std::unique_ptr<StudySubmission>> submit( const QString &algorithmId,
                                                            const QJsonObject &pointParameters,
                                                            const QString &correlationId,
                                                            std::chrono::milliseconds timeout ) = 0;
};

} // namespace sicnu::study
