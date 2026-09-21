// study_runner.h — the windowed study runner (RS14-07).
//
// Executes every sampled study point through the injected IStudyExecutionBackend
// (production adapter: the ExecutionPlane spine) and records each point as an
// ExperimentRun in the existing ExperimentStore, linked to its point identity
// through the MatrixLedger. This module owns NO thread pool and NO queue of
// its own beyond the in-flight submission window — admission stays with
// TaskCenter; run truth stays with the store.
//
// Truthful-state contract (ADR 0130): every SUBMITTED point ends as exactly
// one of
//   recorded  (operator metrics + output artifact on the run)
//   failed    (typed error evidence under the run's metrics["error"])
//   cancelled (cancellation reason recorded)
// A point is never silently dropped, retried behind the caller's back, or
// reported as success without committed output. Submit refusals become
// failed runs ("study.run_submit_refused") so the submitted set stays
// complete. Points a cancellation prevented from submitting are NOT given
// fake runs — they surface as "missing" in the report's status accounting.
#pragma once

#include "study/study_execution.h"
#include "study/study_sampling.h"

#include <QVector>

#include <atomic>
#include <functional>

namespace sicnu::experiment
{
class ExperimentStore;
class MatrixLedger;
}

namespace sicnu::study
{

struct StudyProgress
{
    enum class Phase
    {
        Started,        ///< sampling done, submission window opening
        PointSubmitted, ///< a point entered the in-flight window
        PointFinished,  ///< a point reached a terminal, recorded state
        Finished,       ///< every point terminal
        Cancelled,      ///< caller cancel observed; all in-flight drained as cancelled
        Aborted,        ///< the store refused a truthful write; further runs impossible
    };

    Phase phase = Phase::Started;
    QString pointId;
    int totalPoints = 0;
    int recordedCount = 0;
    int failedCount = 0;
    int cancelledCount = 0;
    int inFlight = 0;
    QString message;
};

struct StudyRunSummary
{
    QString experimentId;
    QString studyId;
    int totalPoints = 0;
    int recordedCount = 0;
    int failedCount = 0;
    int cancelledCount = 0;
    /// One entry per point that REACHED a run record — i.e. every submitted
    /// point. Points a cancellation prevented from ever being submitted have
    /// no run (there is no execution to lie about); the report accounts them
    /// as "missing" (totalPoints − terminalCount − neverSubmitted = 0 when
    /// the study was not cancelled early).
    QStringList runIds;
    QString stoppedReason;  ///< "" on natural completion, else "cancelled"/"aborted:<code>"
    qint64 elapsedMs = 0;

    int terminalCount() const { return recordedCount + failedCount + cancelledCount; }
};

class StudyRunner
{
  public:
    /// @p store is the single run-truth authority; @p ledger links point ids
    /// to run ids inside it.
    StudyRunner( experiment::ExperimentStore &store, experiment::MatrixLedger &ledger,
                 IStudyExecutionBackend &backend );

    void setProgressCallback( std::function<void( const StudyProgress & )> callback );

    /// Runs the whole study. @p studyOutputDir receives one committed output
    /// per point under "<studyOutputDir>/<pointId>/output.tif". Cooperative
    /// cancellation: @p cancelFlag is observed between submissions and after
    /// every wait; in-flight points are cancelled and recorded as cancelled.
    /// Typed refusals: study.spec_* (validation), study.budget_exceeded,
    /// study.output_dir_invalid, study.store_unavailable.
    Result<StudyRunSummary> run( const ParameterStudySpec &spec,
                                 const std::atomic<bool> &cancelFlag,
                                 const QString &studyOutputDir );

  private:
    experiment::ExperimentStore *m_store = nullptr;
    experiment::MatrixLedger *m_ledger = nullptr;
    IStudyExecutionBackend *m_backend = nullptr;
    std::function<void( const StudyProgress & )> m_progress;
};

} // namespace sicnu::study
