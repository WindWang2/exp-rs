// study_runner.cpp — windowed execution + truthful run recording.
#include "study/study_runner.h"

#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/run_recorder.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonObject>

#include <deque>

namespace sicnu::study
{
namespace
{

Diagnostic runnerError( const QString &code, const QString &message )
{
    Diagnostic d;
    d.code = code;
    d.message = message;
    d.severity = sicnu::data::DiagnosticSeverity::Error;
    return d;
}

/// Only metrics the spec declared AND the run actually reported are recorded
/// per point — absence is meaningful (the aggregator's runCount reflects it,
/// no imputation anywhere).
QJsonObject selectMetrics( const QStringList &metricNames, const QJsonObject &payload )
{
    QJsonObject metrics;
    for ( const QString &name : metricNames )
    {
        const auto value = payload.value( name );
        if ( value.isDouble() )
            metrics.insert( name, value.toDouble() );
    }
    return metrics;
}

} // namespace

StudyRunner::StudyRunner( experiment::ExperimentStore &store, experiment::MatrixLedger &ledger,
                          IStudyExecutionBackend &backend )
    : m_store( &store )
    , m_ledger( &ledger )
    , m_backend( &backend )
{
}

void StudyRunner::setProgressCallback( std::function<void( const StudyProgress & )> callback )
{
    m_progress = std::move( callback );
}

Result<StudyRunSummary> StudyRunner::run( const ParameterStudySpec &spec,
                                          const std::atomic<bool> &cancelFlag,
                                          const QString &studyOutputDir )
{
    const auto validated = spec.validate();
    if ( !validated )
        return Result<StudyRunSummary>::failure( validated.diagnostics() );

    auto sampled = sampleStudyPoints( spec );
    if ( !sampled )
        return Result<StudyRunSummary>::failure( sampled.diagnostics() );
    const QVector<StudyPoint> points = sampled.take();

    // One stable output location per point; the commit policy moves the
    // operator's temporary there atomically (never an uncommitted temporary
    // path, #1056).
    if ( studyOutputDir.isEmpty() || !QDir::isAbsolutePath( studyOutputDir ) )
        return Result<StudyRunSummary>::failure( runnerError(
            QStringLiteral( "study.output_dir_invalid" ),
            QStringLiteral( "studyOutputDir must be a non-empty absolute path" ) ) );
    const QDir outputDir( studyOutputDir );
    if ( !outputDir.mkpath( QStringLiteral( "." ) ) )
        return Result<StudyRunSummary>::failure( runnerError(
            QStringLiteral( "study.output_dir_invalid" ),
            QStringLiteral( "cannot create study output directory %1" ).arg( studyOutputDir ) ) );

    // The study records into the experiment authority; create it when missing,
    // never touch it otherwise (idempotent by experiment id).
    if ( !m_store->experimentById( spec.experimentId ) )
    {
        experiment::Experiment experiment;
        experiment.setExperimentId( spec.experimentId );
        experiment.setName( spec.studyId );
        experiment.setObjective( QStringLiteral( "Parameter sensitivity study of %1 (%2)"
                                                  " — RS14-07 parameter studio" )
                                     .arg( spec.algorithmId,
                                           samplingStrategyToString( spec.strategy ) ) );
        experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
        experiment.tags().append( QStringLiteral( "parameter-study" ) );
        const auto stored = m_store->upsertExperiment( experiment );
        if ( !stored )
            return Result<StudyRunSummary>::failure( runnerError(
                QStringLiteral( "study.store_unavailable" ),
                QStringLiteral( "cannot create the study experiment %1: %2" )
                    .arg( spec.experimentId, stored.diagnostics().first().message ) ) );
    }

    StudyRunSummary summary;
    summary.studyId = spec.studyId;
    summary.experimentId = spec.experimentId;
    summary.totalPoints = points.size();

    experiment::ExperimentRunRecorder recorder( *m_store );

    StudyProgress progress;
    progress.totalPoints = points.size();
    const auto emitProgress = [&]( StudyProgress::Phase phase, const QString &pointId,
                                   const QString &message, int inFlight ) {
        progress.phase = phase;
        progress.pointId = pointId;
        progress.message = message;
        progress.inFlight = inFlight;
        progress.recordedCount = summary.recordedCount;
        progress.failedCount = summary.failedCount;
        progress.cancelledCount = summary.cancelledCount;
        if ( m_progress )
            m_progress( progress );
    };

    struct InFlight
    {
        StudyPoint point;
        std::unique_ptr<StudySubmission> submission;
        QString runId;
    };
    std::deque<InFlight> inFlight;

    // Truthful failure with NO execution behind it (submit refusal): the run
    // exists, carries the typed code under metrics["error"], and an empty
    // executionRef — an execution ref would be a lie here.
    const auto recordSubmitRefusal = [&]( const StudyPoint &point,
                                          const QVector<Diagnostic> &diagnostics ) {
        experiment::RunStartRequest request;
        request.experimentId = spec.experimentId;
        request.algorithmId = spec.algorithmId;
        request.parameters = point.parameters;
        request.seed = point.seed;
        request.determinism = dataset::DeterminismGrade::BestEffort;
        request.determinismNote = QStringLiteral(
            "study sweep: replicate seed recorded; operator determinism is not "
            "asserted by the study layer" );
        request.executionRef = QString(); // truthful: no execution ever existed
        request.parameters.insert( QStringLiteral( "output" ),
                                   outputDir.filePath( point.pointId
                                                       + QStringLiteral( "/output.tif" ) ) );
        const auto runId = recorder.startRun( request );
        if ( !runId )
        {
            emitProgress( StudyProgress::Phase::Aborted, point.pointId,
                          QStringLiteral( "store refused run creation" ), 0 );
            return false;
        }
        QString message;
        for ( const auto &d : diagnostics )
            if ( d.severity == sicnu::data::DiagnosticSeverity::Error )
            {
                message = d.message;
                break;
            }
        recorder.markFailed( runId.value(), QStringLiteral( "study.run_submit_refused" ), message );
        m_ledger->link( point.pointId, runId.value() );
        summary.runIds.append( runId.value() );
        ++summary.failedCount;
        emitProgress( StudyProgress::Phase::PointFinished, point.pointId,
                      QStringLiteral( "submit refused" ), static_cast<int>( inFlight.size() ) );
        return true;
    };

    QElapsedTimer timer;
    timer.start();

    emitProgress( StudyProgress::Phase::Started, QString(),
                  QStringLiteral( "sampled %1 points" ).arg( points.size() ), 0 );

    int nextIndex = 0;
    bool cancelled = false;
    bool aborted = false;
    QString abortCode;

    while ( true )
    {
        if ( cancelFlag.load() && !cancelled )
        {
            cancelled = true;
            for ( InFlight &pending : inFlight )
                pending.submission->cancel();
            emitProgress( StudyProgress::Phase::Cancelled, QString(),
                          QStringLiteral( "cancel observed; draining %1 in-flight points" )
                              .arg( inFlight.size() ),
                          static_cast<int>( inFlight.size() ) );
        }

        if ( nextIndex < points.size() && !cancelled
             && inFlight.size() < static_cast<std::size_t>( spec.budget.maxInFlight ) )
        {
            const StudyPoint &point = points.at( nextIndex );
            ++nextIndex;

            const QString outputPath =
                outputDir.filePath( point.pointId + QStringLiteral( "/output.tif" ) );
            QJsonObject pointParameters = point.parameters;
            pointParameters.insert( QStringLiteral( "output" ), outputPath );
            const QString correlationId = QStringLiteral( "%1/%2#%3" )
                                              .arg( spec.studyId, point.pointId )
                                              .arg( point.replicateIndex );
            const std::chrono::milliseconds timeout( spec.budget.perRunTimeoutMs );

            auto submission = m_backend->submit( spec.algorithmId, pointParameters,
                                                 correlationId, timeout );
            if ( !submission )
            {
                if ( !recordSubmitRefusal( point, submission.diagnostics() ) )
                {
                    aborted = true;
                    abortCode = QStringLiteral( "study.store_unavailable" );
                    break;
                }
                continue;
            }

            experiment::RunStartRequest request;
            request.experimentId = spec.experimentId;
            request.algorithmId = spec.algorithmId;
            request.parameters = pointParameters;
            request.seed = point.seed;
            request.determinism = dataset::DeterminismGrade::BestEffort;
            request.determinismNote = QStringLiteral(
                "study sweep: replicate seed recorded; operator determinism is not "
                "asserted by the study layer" );
            request.executionRef = submission.value()->executionRef();

            const auto runId = recorder.startRun( request );
            if ( !runId )
            {
                // The store refused the truthful write — running on would
                // produce executions with no honest record. Stop, cancel what
                // is in flight, and say so.
                submission.value()->cancel();
                aborted = true;
                abortCode = QStringLiteral( "study.store_unavailable" );
                break;
            }
            inFlight.push_back( InFlight{ point, submission.take(),
                                          runId.value() } );
            emitProgress( StudyProgress::Phase::PointSubmitted, point.pointId,
                          correlationId, static_cast<int>( inFlight.size() ) );
            continue;
        }

        if ( inFlight.empty() )
            break;

        InFlight &front = inFlight.front();
        const StudyExecutionOutcome outcome =
            front.submission->wait( std::chrono::milliseconds( spec.budget.perRunTimeoutMs ) );

        QString finishMessage;
        if ( outcome.status == StudyExecutionOutcome::Status::Succeeded )
        {
            const QString output = outcome.payload.value( QStringLiteral( "output" ) ).toString();
            if ( output.isEmpty() )
            {
                recorder.markFailed( front.runId, QStringLiteral( "study.run_missing_output" ),
                                     QStringLiteral( "succeeded execution reported no committed "
                                                      "output path" ) );
                ++summary.failedCount;
                finishMessage = QStringLiteral( "missing committed output" );
            }
            else
            {
                experiment::ExperimentRun::Artifact artifact;
                artifact.path = output;
                artifact.role = QStringLiteral( "output" );
                recorder.markSucceeded( front.runId, { artifact },
                                        selectMetrics( spec.metricNames, outcome.payload ) );
                ++summary.recordedCount;
                finishMessage = QStringLiteral( "recorded" );
            }
        }
        else if ( outcome.status == StudyExecutionOutcome::Status::Cancelled )
        {
            recorder.markCancelled( front.runId,
                                    QStringLiteral( "cancelled: %1" )
                                        .arg( outcome.errorMessage.isEmpty()
                                                  ? QStringLiteral( "external cancel" )
                                                  : outcome.errorMessage ) );
            ++summary.cancelledCount;
            finishMessage = QStringLiteral( "cancelled" );
        }
        else if ( outcome.status == StudyExecutionOutcome::Status::TimedOut )
        {
            recorder.markFailed( front.runId, QStringLiteral( "study.run_timeout" ),
                                 QStringLiteral( "per-run deadline (%1 ms) expired; the "
                                                  "execution was cancelled" )
                                     .arg( spec.budget.perRunTimeoutMs ) );
            ++summary.failedCount;
            finishMessage = QStringLiteral( "timeout" );
        }
        else
        {
            recorder.markFailed( front.runId,
                                 outcome.errorCode.isEmpty()
                                     ? QStringLiteral( "study.operator_failed" )
                                     : outcome.errorCode,
                                 outcome.errorMessage );
            ++summary.failedCount;
            finishMessage = QStringLiteral( "failed" );
        }

        // Every terminal run is linked to its point identity — failed and
        // cancelled runs too, so aggregates report them truthfully.
        m_ledger->link( front.point.pointId, front.runId );
        summary.runIds.append( front.runId );
        emitProgress( StudyProgress::Phase::PointFinished, front.point.pointId, finishMessage,
                      static_cast<int>( inFlight.size() ) - 1 );
        inFlight.pop_front();
    }

    // Abort drain: no truthful record path exists (the store refused), so
    // stop the executions and surface the reason — no fake terminal states.
    if ( aborted )
    {
        for ( InFlight &pending : inFlight )
            pending.submission->cancel();
        for ( InFlight &pending : inFlight )
            pending.submission->wait( std::chrono::milliseconds( spec.budget.perRunTimeoutMs ) );
        summary.stoppedReason = QStringLiteral( "aborted:%1" ).arg( abortCode );
        emitProgress( StudyProgress::Phase::Aborted, QString(), summary.stoppedReason, 0 );
    }
    else
    {
        summary.stoppedReason = cancelled ? QStringLiteral( "cancelled" ) : QString();
        emitProgress( cancelled ? StudyProgress::Phase::Cancelled
                                : StudyProgress::Phase::Finished,
                      QString(), summary.stoppedReason, 0 );
    }

    summary.elapsedMs = timer.elapsed();
    return Result<StudyRunSummary>::success( summary );
}

} // namespace sicnu::study
